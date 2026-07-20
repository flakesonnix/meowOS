// Disk/network-free unit tests for resolver diagnostics (phase 1: `explain`
// data sources; phase 2: `why-not` via the resolver's diagnostic object).
// Uses a synthetic repository and an on-disk temp database; no downloads.

#include <cassert>
#include <iostream>
#include <filesystem>
#include <string>

#include <meow/database/database.hpp>
#include <meow/dependency/resolver.hpp>
#include <meow/error/error.hpp>
#include <meow/package/package.hpp>
#include <meow/repository/repository.hpp>
#include <meow/types/types.hpp>

using namespace meow;
using namespace meow::dependency;
using namespace meow::types;
using namespace meow::repository;

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

Repository makeRepo() {
    Repository repo;
    repo.name = "test";

    auto mk = [](const std::string& name,
                 const std::vector<std::string>& deps,
                 const std::vector<std::string>& conflicts,
                 const std::vector<std::string>& provides) {
        RepositoryPackage p;
        p.name = PackageName{name};
        for (const auto& d : deps) p.depends.push_back({PackageName{d}, {}});
        for (const auto& c : conflicts) p.conflicts.push_back(PackageName{c});
        for (const auto& pr : provides) p.provides.push_back(PackageName{pr});
        return p;
    };

    repo.packages.push_back(mk("libfoo", {}, {}, {"foo-lib"}));
    repo.packages.push_back(mk("hello", {}, {}, {}));
    repo.packages.push_back(mk("app2", {"libfoo>=2.0"}, {}, {}));
    repo.packages.push_back(mk("foo", {}, {"bar"}, {}));
    return repo;
}

package::PackageFile makePkg(const std::string& name) {
    package::PackageFile pkg;
    pkg.metadata.name = PackageName{name};
    pkg.metadata.version = PackageVersion{"1.0.0"};
    pkg.metadata.architecture = CpuArch::AMD64;
    return pkg;
}

void testExplainData() {
    auto db = database::openDatabase(std::filesystem::temp_directory_path() /
                                     "meow_explain_test.db");

    // explicit
    auto exp = makePkg("hello");
    database::registerPackage(db, exp, {});
    database::setInstallReason(db, exp.metadata.name, database::InstallReason::Explicit);
    auto r = database::installReason(db, exp.metadata.name);
    expectPass("explain explicit package", r && *r == database::InstallReason::Explicit);

    // dependency (app requires hello)
    auto depPkg = makePkg("app");
    depPkg.metadata.dependencies.value.push_back(
        types::Dependency{PackageName{"hello"}, {}});
    database::registerPackage(db, depPkg, {});
    database::setInstallReason(db, depPkg.metadata.name, database::InstallReason::Dependency);
    auto rd = database::installReason(db, depPkg.metadata.name);
    expectPass("explain dependency package", rd && *rd == database::InstallReason::Dependency);
    auto req = database::requiredBy(db, PackageName{"hello"});
    bool helloRequiredByApp = false;
    for (const auto& n : req) if (n.value == "app") helloRequiredByApp = true;
    expectPass("explain required-by", helloRequiredByApp);

    // group (GroupMember)
    auto grp = makePkg("gcc");
    database::registerPackage(db, grp, {});
    database::setInstallReason(db, grp.metadata.name, database::InstallReason::GroupMember);
    auto rg = database::installReason(db, grp.metadata.name);
    expectPass("explain group package", rg && *rg == database::InstallReason::GroupMember);

    // provider (repo metadata provides)
    auto repo = makeRepo();
    const auto* libfoo = findPackage(repo, PackageName{"libfoo"});
    bool providesFooLib = false;
    if (libfoo) for (const auto& pr : libfoo->provides)
        if (pr.value == "foo-lib") providesFooLib = true;
    expectPass("explain provider", providesFooLib);

    database::closeDatabase(db);
    std::error_code ec;
    std::filesystem::remove(std::filesystem::temp_directory_path() / "meow_explain_test.db", ec);
}

void testWhyNot() {
    auto repo = makeRepo();
    auto db = database::openDatabase(std::filesystem::temp_directory_path() /
                                     "meow_whynot_test.db");

    std::vector<ResolveDiagnostic> diags;
    auto kinds = [&](ResolveDiagnostic::Kind k) {
        for (const auto& d : diags) if (d.kind == k) return true;
        return false;
    };

    // missing package
    diags.clear();
    expectPass("why-not missing package",
               !tryResolve(repo, PackageName{"nonexistent"}, db, diags) &&
               kinds(ResolveDiagnostic::Kind::MissingPackage));

    // version constraint: app2 requires libfoo>=2.0 but libfoo is 1.0.0
    diags.clear();
    bool vc = !tryResolve(repo, PackageName{"app2"}, db, diags) &&
              kinds(ResolveDiagnostic::Kind::VersionConflict);
    bool vcDetail = false;
    for (const auto& d : diags) {
        if (d.kind == ResolveDiagnostic::Kind::VersionConflict &&
            d.package.value == "libfoo" &&
            d.requiredVersion.find("2.0") != std::string::npos &&
            d.availableVersion == "1.0.0") {
            vcDetail = true;
        }
    }
    expectPass("why-not version constraint", vc && vcDetail);

    // conflict: foo conflicts with bar, and bar is installed
    auto bar = makePkg("bar");
    database::registerPackage(db, bar, {});
    diags.clear();
    bool cf = !tryResolve(repo, PackageName{"foo"}, db, diags) &&
              kinds(ResolveDiagnostic::Kind::PackageConflict);
    expectPass("why-not conflict", cf);

    // missing provider: openssl3 has no provider in the repo
    diags.clear();
    expectPass("why-not missing provider",
               !tryResolve(repo, PackageName{"openssl3"}, db, diags) &&
               kinds(ResolveDiagnostic::Kind::MissingProvider));

    // determinism: same inputs produce the same diagnostics
    std::vector<ResolveDiagnostic> a, b;
    tryResolve(repo, PackageName{"app2"}, db, a);
    tryResolve(repo, PackageName{"app2"}, db, b);
    bool same = a.size() == b.size();
    for (size_t i = 0; same && i < a.size(); ++i) {
        same = a[i].kind == b[i].kind && a[i].package.value == b[i].package.value;
    }
    expectPass("diagnostics deterministic", same);

    database::closeDatabase(db);
    std::error_code ec;
    std::filesystem::remove(std::filesystem::temp_directory_path() / "meow_whynot_test.db", ec);
}

}  // namespace

int main() {
    std::cout << "=== resolver diagnostics unit tests ===\n";
    testExplainData();
    testWhyNot();
    if (failures == 0) {
        std::cout << "all resolver diagnostics tests passed\n";
        return 0;
    }
    std::cout << failures << " resolver diagnostics test(s) failed\n";
    return 1;
}
