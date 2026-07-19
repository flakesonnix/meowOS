// Disk/network-free unit tests for optional-dependency expansion (phase 2).
// Exercises expandInstallRequest: optional packages are promoted to requested
// roots before dependency resolution, and an invalid selection is rejected.
// No downloads/installs occur — expansion only resolves package names.

#include <cassert>
#include <iostream>
#include <string>

#include <meow/dependency/resolver.hpp>
#include <meow/error/error.hpp>
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

template <typename Fn>
void expectThrow(const std::string& what, error::ErrorCode expectCode, Fn fn) {
    try {
        fn();
        std::cout << "  FAIL: " << what << " (no exception)\n";
        ++failures;
    } catch (const error::MeowError& e) {
        if (e.code == expectCode) {
            std::cout << "  PASS: " << what << "\n";
        } else {
            std::cout << "  FAIL: " << what << " (wrong code)\n";
            ++failures;
        }
    }
}

Repository makeRepo() {
    Repository repo;
    repo.name = "test";

    auto mk = [](const std::string& name,
                 const std::vector<std::string>& deps,
                 const std::vector<std::string>& optionals) {
        RepositoryPackage p;
        p.name = PackageName{name};
        for (const auto& d : deps) p.depends.push_back(PackageName{d});
        for (const auto& o : optionals) {
            OptionalDependency od;
            od.package = PackageName{o};
            p.optionalDepends.push_back(std::move(od));
        }
        return p;
    };

    repo.packages.push_back(mk("app", {"hello"}, {"gtk4", "qt6"}));
    repo.packages.push_back(mk("hello", {}, {}));
    repo.packages.push_back(mk("gtk4", {}, {}));
    repo.packages.push_back(mk("qt6", {}, {}));
    return repo;
}

std::set<std::string> names(const std::vector<PackageName>& v) {
    std::set<std::string> s;
    for (const auto& n : v) s.insert(n.value);
    return s;
}

void testExpansion() {
    auto repo = makeRepo();

    // metadata only: no optional packages are pulled in. The closure is
    // resolved later; expansion returns only the requested root.
    {
        InstallRequest req;
        req.packages.push_back(PackageName{"app"});
        auto roots = expandInstallRequest(repo, req);
        auto s = names(roots);
        expectPass("metadata only doesn't install optional",
                   s == std::set<std::string>{"app"});
    }

    // --with-optional: every declared optional becomes a root.
    {
        InstallRequest req;
        req.packages.push_back(PackageName{"app"});
        req.includeAllOptional = true;
        auto roots = expandInstallRequest(repo, req);
        auto s = names(roots);
        expectPass("--with-optional installs all optional",
                   s == std::set<std::string>{"app", "gtk4", "qt6"});
    }

    // --optional gtk4: only the selected one is added.
    {
        InstallRequest req;
        req.packages.push_back(PackageName{"app"});
        req.selectedOptional.insert(PackageName{"gtk4"});
        auto roots = expandInstallRequest(repo, req);
        auto s = names(roots);
        expectPass("--optional installs selected package",
                   s == std::set<std::string>{"app", "gtk4"});
    }

    // multiple --optional flags.
    {
        InstallRequest req;
        req.packages.push_back(PackageName{"app"});
        req.selectedOptional.insert(PackageName{"gtk4"});
        req.selectedOptional.insert(PackageName{"qt6"});
        auto roots = expandInstallRequest(repo, req);
        auto s = names(roots);
        expectPass("multiple --optional flags",
                   s == std::set<std::string>{"app", "gtk4", "qt6"});
    }

    // invalid optional (not declared) rejected before resolution.
    expectThrow("--optional nonexistent rejected",
                 error::ErrorCode::DependencyNotFound, [&] {
                     InstallRequest req;
                     req.packages.push_back(PackageName{"app"});
                     req.selectedOptional.insert(PackageName{"nonexistent"});
                     expandInstallRequest(repo, req);
                 });

    // the key invariant: optionals are promoted to requested roots, so the
    // installer records them as Explicit (the same requested set it is given).
    {
        InstallRequest req;
        req.packages.push_back(PackageName{"app"});
        req.includeAllOptional = true;
        auto roots = expandInstallRequest(repo, req);
        // Every root is something the user explicitly asked for (the package
        // or a chosen optional), so the install path treats them all as
        // Explicit — no separate "optional" reason is introduced.
        bool allExplicitRoots = !roots.empty();
        expectPass("optional packages promoted to requested roots",
                   allExplicitRoots &&
                   names(roots).count("gtk4") == 1 &&
                   names(roots).count("qt6") == 1);
    }
}

}  // namespace

int main() {
    std::cout << "=== optional dependency expansion unit tests ===\n";
    testExpansion();
    if (failures == 0) {
        std::cout << "all optional expansion tests passed\n";
        return 0;
    }
    std::cout << failures << " optional expansion test(s) failed\n";
    return 1;
}
