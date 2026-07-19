// Disk/network-free unit tests for the package history + install-reason
// layer. Exercises reason upgrade rules, append-only history, transaction
// grouping, and migration of a v1 (reason-less) database. Uses a throwaway
// on-disk SQLite database in a temp directory.

#include <cassert>
#include <ctime>
#include <iostream>
#include <filesystem>

#include <meow/database/database.hpp>
#include <meow/error/error.hpp>
#include <sqlite3.h>
#include <meow/format/version.hpp>
#include <meow/package/package.hpp>
#include <meow/types/types.hpp>

using namespace meow;
using namespace meow::database;
using namespace meow::package;
using namespace meow::types;

namespace {
int failures = 0;

void expectPass(const std::string& what, bool ok) {
    if (ok) {
        std::cout << "  PASS: " << what << "\n";
    } else {
        std::cout << "  FAIL: " << what << "\n";
        ++failures;
    }
}

PackageFile makePkg(const std::string& name, const std::string& version) {
    PackageFile pkg;
    pkg.metadata.name = PackageName{name};
    pkg.metadata.version = PackageVersion{version};
    pkg.metadata.architecture = CpuArch::AMD64;
    return pkg;
}

Database openTempDb(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::remove(p, ec);
    return openDatabase(p);
}
}

int main() {
    std::error_code ec;
    auto tmp = std::filesystem::temp_directory_path() / "meow_history_test.db";
    auto db = openTempDb(tmp);

    std::cout << "=== package history unit tests ===\n";

    // explicit install records reason
    {
        auto pkg = makePkg("hello", "1.1.0");
        registerPackage(db, pkg, {});
        setInstallReason(db, pkg.metadata.name, InstallReason::Explicit);
        recordHistory(db, "install", pkg.metadata.name, pkg.metadata.version,
                      InstallReason::Explicit, "tx1");
        auto r = installReason(db, pkg.metadata.name);
        expectPass("explicit install records reason",
                   r && *r == InstallReason::Explicit);
    }

    // dependency install records dependency
    {
        auto pkg = makePkg("libfoo", "1.0.0");
        registerPackage(db, pkg, {});
        setInstallReason(db, pkg.metadata.name, InstallReason::Dependency);
        recordHistory(db, "install", pkg.metadata.name, pkg.metadata.version,
                      InstallReason::Dependency, "tx1");
        auto r = installReason(db, pkg.metadata.name);
        expectPass("dependency install records dependency",
                   r && *r == InstallReason::Dependency);
    }

    // explicit reason survives later group install (no downgrade)
    {
        auto pkg = makePkg("gcc", "13.0.0");
        registerPackage(db, pkg, {});
        setInstallReason(db, pkg.metadata.name, InstallReason::Explicit);
        setInstallReason(db, pkg.metadata.name, InstallReason::GroupMember);
        auto r = installReason(db, pkg.metadata.name);
        expectPass("explicit reason survives later group install",
                   r && *r == InstallReason::Explicit);
    }

    // group member reason recorded, not downgraded by dependency
    {
        auto pkg = makePkg("make", "4.4.0");
        registerPackage(db, pkg, {});
        setInstallReason(db, pkg.metadata.name, InstallReason::GroupMember);
        setInstallReason(db, pkg.metadata.name, InstallReason::Dependency);
        auto r = installReason(db, pkg.metadata.name);
        expectPass("group member not downgraded to dependency",
                   r && *r == InstallReason::GroupMember);
    }

    // transaction id groups one install
    {
        auto a = makePkg("app", "2.0.0");
        auto b = makePkg("app-lib", "2.0.0");
        registerPackage(db, a, {});
        registerPackage(db, b, {});
        recordHistory(db, "install", a.metadata.name, a.metadata.version,
                      InstallReason::Explicit, "tx-group");
        recordHistory(db, "install", b.metadata.name, b.metadata.version,
                      InstallReason::Dependency, "tx-group");
        auto ha = packageHistory(db, a.metadata.name);
        auto hb = packageHistory(db, b.metadata.name);
        expectPass("transaction id groups one install",
                   !ha.empty() && !hb.empty() &&
                   ha[0].transactionId == "tx-group" &&
                   hb[0].transactionId == "tx-group");
    }

    // remove records history
    {
        auto pkg = makePkg("hello", "1.1.0");
        recordHistory(db, "remove", pkg.metadata.name, pkg.metadata.version,
                      InstallReason::Explicit, "tx-rm");
        auto h = packageHistory(db, pkg.metadata.name);
        bool hasRemove = false;
        for (const auto& e : h) if (e.action == "remove") hasRemove = true;
        expectPass("remove records history", hasRemove);
    }

    // history survives database reopen (append-only, not the source of truth)
    {
        closeDatabase(db);
        db = openDatabase(tmp);
        auto h = packageHistory(db);
        expectPass("history survives database reopen",
                   !h.empty() &&
                   h.front().package == "hello" &&
                   h.front().action == "install");
    }

    // explicitly-installed lists only Explicit
    {
        auto ex = explicitlyInstalled(db);
        bool hasHello = false, hasGcc = false, hasLibfoo = false;
        for (const auto& n : ex) {
            if (n.value == "hello") hasHello = true;
            if (n.value == "gcc") hasGcc = true;
            if (n.value == "libfoo") hasLibfoo = true;
        }
        expectPass("explicitly-installed lists explicit only",
                   hasHello && hasGcc && !hasLibfoo);
    }

    // migration from v1 database (no install_reason / package_history)
    {
        auto v1 = std::filesystem::temp_directory_path() / "meow_v1_test.db";
        std::error_code ec;
        std::filesystem::remove(v1, ec);
        sqlite3* h;
        sqlite3_open(v1.c_str(), &h);
        const char* sql =
            "CREATE TABLE packages ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  name TEXT NOT NULL UNIQUE,"
            "  version TEXT NOT NULL,"
            "  architecture TEXT NOT NULL,"
            "  install_time INTEGER NOT NULL"
            ");"
            "CREATE TABLE metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL);"
            "INSERT INTO packages (name, version, architecture, install_time)"
            "  VALUES ('legacy', '1.0.0', 'amd64', 0);"
            "INSERT INTO metadata (key, value) VALUES ('schema_version', '1');";
        char* err = nullptr;
        sqlite3_exec(h, sql, nullptr, nullptr, &err);
        if (err) sqlite3_free(err);
        sqlite3_close(h);

        auto mdb = openDatabase(v1);
        auto r = installReason(mdb, PackageName{"legacy"});
        // v1 packages default to Dependency until a reason is recorded.
        expectPass("migration from v1 database defaults reason",
                   r && *r == InstallReason::Dependency);
        auto h2 = packageHistory(mdb);
        expectPass("migration creates package_history table",
                   checkSchema(mdb) &&
                   h2.empty());
        closeDatabase(mdb);
    }

    std::filesystem::remove(tmp, ec);
    if (failures == 0) {
        std::cout << "all package history tests passed\n";
        return 0;
    }
    std::cout << failures << " package history test(s) failed\n";
    return 1;
}
