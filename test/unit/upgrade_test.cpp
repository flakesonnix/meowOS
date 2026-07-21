#include <meow/database/database.hpp>
#include <meow/database/migration.hpp>
#include <meow/error/error.hpp>
#include <meow/format/version.hpp>
#include <meow/package/package.hpp>
#include <meow/transaction/transaction.hpp>
#include <meow/types/types.hpp>

#include <sqlite3.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace meow;
using namespace meow::database;
using namespace meow::package;
using namespace meow::transaction;
using namespace meow::types;

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%d)\n", #cond, __LINE__); ++failures; } \
} while (0)

static PackageFile makePkg(const std::string& name, const std::string& version) {
    PackageFile pkg;
    pkg.metadata.name = PackageName{name};
    pkg.metadata.version = PackageVersion{version};
    pkg.metadata.architecture = CpuArch::AMD64;
    return pkg;
}

static Database openTempDb(const fs::path& p) {
    std::error_code ec;
    fs::remove(p, ec);
    return openDatabase(p);
}


// ── Bug A: Upgrade must clean up old file records ──────────────────────────

// When upgradePackage runs, it sets clearExistingFiles=true so that old file
// entries in the DB are deleted before the new version's files are inserted.
static void test_upgrade_clears_old_files() {
    auto tmp = fs::temp_directory_path() / "meow_upgrade_files.db";
    auto db = openTempDb(tmp);

    auto v1 = makePkg("myapp", "1.0.0");
    std::vector<fs::path> oldFiles = {
        fs::temp_directory_path() / "meow_upgrade_old_a",
        fs::temp_directory_path() / "meow_upgrade_old_b",
    };
    for (const auto& f : oldFiles) { std::ofstream of(f); of << "old"; }
    try {
        registerPackage(db, v1, oldFiles);
    } catch (std::exception& ex) {
        std::fprintf(stderr, "FAIL: registerPackage v1 threw: %s\n", ex.what());
        throw;
    }
    CHECK( listPackageFiles(db, v1.metadata.name).size() == 2 );

    auto v2 = makePkg("myapp", "2.0.0");
    std::vector<fs::path> newFiles = {
        fs::temp_directory_path() / "meow_upgrade_new_a",
    };
    for (const auto& f : newFiles) { std::ofstream of(f); of << "new"; }

    Transaction tx = beginTransaction();
    Transaction::PackageEntry entry;
    entry.pkg = v2;
    entry.installedFiles = newFiles;
    entry.clearExistingFiles = true;
    tx.packages.push_back(std::move(entry));
    commitTransaction(tx, db);

    // Old file records must be gone, only new files remain
    auto remaining = listPackageFiles(db, v1.metadata.name);
    CHECK( remaining.size() == 1 );
    CHECK( remaining[0] == newFiles[0] );

    std::error_code ec;
    for (const auto& f : oldFiles) fs::remove(f, ec);
    for (const auto& f : newFiles) fs::remove(f, ec);
    closeDatabase(db);
    fs::remove(tmp, ec);
}

// Without clearExistingFiles, existing file records are preserved.
static void test_fresh_install_no_clear() {
    auto tmp = fs::temp_directory_path() / "meow_fresh_clear.db";
    auto db = openTempDb(tmp);

    auto v1 = makePkg("other", "1.0.0");
    std::vector<fs::path> oldFiles = {
        fs::temp_directory_path() / "meow_fresh_old",
    };
    { std::ofstream of(oldFiles[0]); of << "old"; }
    registerPackage(db, v1, oldFiles);

    auto v2 = makePkg("other", "2.0.0");
    std::vector<fs::path> newFiles = {
        fs::temp_directory_path() / "meow_fresh_new",
    };
    { std::ofstream of(newFiles[0]); of << "new"; }

    Transaction tx = beginTransaction();
    Transaction::PackageEntry entry;
    entry.pkg = v2;
    entry.installedFiles = newFiles;
    // clearExistingFiles defaults to false: old records stay.
    tx.packages.push_back(std::move(entry));
    commitTransaction(tx, db);

    // Both old and new file records present because clearExistingFiles was false
    auto all = listPackageFiles(db, PackageName{"other"});
    CHECK( all.size() == 2 );

    std::error_code ec;
    for (const auto& f : oldFiles) fs::remove(f, ec);
    for (const auto& f : newFiles) fs::remove(f, ec);
    closeDatabase(db);
    fs::remove(tmp, ec);
}

// ── Bug B: Migration must be atomic ────────────────────────────────────────

// Build a v1 database (schema_version=1, no install_reason, no v2 tables) by
// opening a fresh v2 DB, then stripping it back to v1 shape.
static fs::path createV1Database(const fs::path& path) {
    std::error_code ec;
    fs::remove(path, ec);
    {
        auto db = openDatabase(path);
        execRaw(db, "UPDATE metadata SET value = '1' WHERE key = 'schema_version';");
        execRaw(db, "DROP TABLE IF EXISTS package_history;");
        execRaw(db, "DROP TABLE IF EXISTS package_deps;");
        execRaw(db, "DROP TABLE IF EXISTS package_provides;");
        execRaw(db, "DROP TABLE IF EXISTS files;");
        execRaw(db,
            "CREATE TABLE packages_v1 ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  name TEXT NOT NULL UNIQUE,"
            "  version TEXT NOT NULL,"
            "  architecture TEXT NOT NULL,"
            "  install_time INTEGER NOT NULL"
            ");");
        execRaw(db,
            "INSERT INTO packages_v1 (id, name, version, architecture, install_time) "
            "SELECT id, name, version, architecture, install_time FROM packages;");
        execRaw(db, "DROP TABLE packages;");
        execRaw(db, "ALTER TABLE packages_v1 RENAME TO packages;");
        execRaw(db,
            "INSERT INTO packages (name, version, architecture, install_time) "
            "VALUES ('legacy', '1.0.0', 'amd64', 0);");
        closeDatabase(db);
    }
    return path;
}

// Migration from v1 produces complete v2 schema.
static void test_migration_v1_to_v2() {
    auto tmp = fs::temp_directory_path() / "meow_migrate_v1.db";
    createV1Database(tmp);

    auto db = openDatabase(tmp);
    CHECK( checkSchema(db) );

    // install_reason column was added by migration; legacy package defaults
    // to Dependency.
    auto r = installReason(db, PackageName{"legacy"});
    CHECK( r && *r == InstallReason::Dependency );

    // History table exists and is empty.
    auto h = packageHistory(db);
    CHECK( h.empty() );

    closeDatabase(db);
    std::error_code ec;
    fs::remove(tmp, ec);
}

// Re-opening a v2 database is safe (idempotent).
static void test_migration_idempotent() {
    auto tmp = fs::temp_directory_path() / "meow_migrate_idem.db";
    std::error_code ec;
    fs::remove(tmp, ec);
    {
        auto db = openTempDb(tmp);
        closeDatabase(db);
    }
    {
        auto db = openDatabase(tmp);
        CHECK( checkSchema(db) );
        closeDatabase(db);
    }
    fs::remove(tmp, ec);
}

int main() {
    test_upgrade_clears_old_files();
    test_fresh_install_no_clear();
    test_migration_v1_to_v2();
    test_migration_idempotent();

    if (failures == 0) {
        std::printf("all upgrade/migration tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
}
