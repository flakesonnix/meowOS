// Disk/network-free unit tests for the in-memory repository backend.
// Exercises Metadata load, single-package load, artifact fetch (present and
// missing), dependency closure resolution, and conflicting-package metadata -
// all without touching the filesystem or network.

#include <cassert>
#include <iostream>
#include <map>
#include <string>

#include <meow/error/error.hpp>
#include <meow/package/package.hpp>
#include <meow/package/parser.hpp>
#include <meow/repository/backend.hpp>
#include <meow/repository/memory_backend.hpp>
#include <meow/repository/repository.hpp>
#include <meow/types/types.hpp>

using namespace meow;
using namespace meow::repository;
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
    repo.id = "test-00000000000000000000000000000000";

    RepositoryPackage lib;
    lib.name = PackageName{"libfoo"};
    lib.description = Description{"lib"};
    RepositoryVersion libv;
    libv.version = PackageVersion{"1.0.0"};
    libv.artifact = PackageArtifact{"libfoo-1.0.0.pkg.tar.zst", "memory://libfoo",
                                    "deadbeef"};
    lib.versions.push_back(libv);

    RepositoryPackage app;
    app.name = PackageName{"app"};
    app.description = Description{"app"};
    app.depends.push_back(PackageName{"libfoo"});
    RepositoryVersion appv;
    appv.version = PackageVersion{"2.0.0"};
    appv.artifact = PackageArtifact{"app-2.0.0.pkg.tar.zst", "memory://app",
                                    "cafe"};
    app.versions.push_back(appv);

    repo.packages.push_back(lib);
    repo.packages.push_back(app);
    return repo;
}

package::PackageFile makePkgFile(const std::string& name,
                                 const std::string& version) {
    package::PackageFile pf;
    pf.archivePath = name;
    pf.metadata.name = PackageName{name};
    pf.metadata.version = PackageVersion{version};
    return pf;
}

void testLoadAndResolve() {
    auto repo = makeRepo();
    std::map<std::string, package::PackageFile> arts;
    arts["libfoo-1.0.0.pkg.tar.zst"] = makePkgFile("libfoo", "1.0.0");
    arts["app-2.0.0.pkg.tar.zst"] = makePkgFile("app", "2.0.0");

    MemoryRepositoryBackend backend(std::move(repo), std::move(arts));

    auto loaded = backend.loadRepository();
    expectPass("loadRepository returns in-memory repo",
               loaded.packages.size() == 2 && loaded.name == "test");

    expectPass("loadPackage finds existing",
               backend.loadPackage(PackageName{"app"}).name.value == "app");
    expectThrow("loadPackage throws PackageNotFound",
                error::ErrorCode::PackageNotFound, [&] {
                    backend.loadPackage(PackageName{"ghost"});
                });

    auto names = listPackages(loaded);
    expectPass("listPackages enumerates both packages",
               names.size() == 2);

    auto closure = resolveDependencyNames(loaded, PackageName{"app"});
    bool hasApp = false, hasLib = false;
    for (const auto& n : closure)
        if (n.value == "app") hasApp = true;
        else if (n.value == "libfoo") hasLib = true;
    expectPass("resolveDependencyNames closes app -> libfoo", hasApp && hasLib);
}

void testArtifactFetch() {
    auto repo = makeRepo();
    std::map<std::string, package::PackageFile> arts;
    arts["libfoo-1.0.0.pkg.tar.zst"] = makePkgFile("libfoo", "1.0.0");

    MemoryRepositoryBackend backend(std::move(repo), std::move(arts));

    auto pf = backend.fetchArtifact(
        PackageArtifact{"libfoo-1.0.0.pkg.tar.zst", "memory://libfoo", "x"});
    expectPass("fetchArtifact returns preloaded PackageFile",
               pf.metadata.name.value == "libfoo" &&
                   pf.metadata.version.value == "1.0.0");

    expectThrow("fetchArtifact throws FileNotFound for missing artifact",
                error::ErrorCode::FileNotFound, [&] {
                    backend.fetchArtifact(PackageArtifact{
                        "app-2.0.0.pkg.tar.zst", "memory://app", "x"});
                });
}

void testConflictingPackages() {
    Repository repo;
    repo.name = "conflict";
    repo.id = "conf-00000000000000000000000000000000";

    RepositoryPackage a;
    a.name = PackageName{"editor-a"};
    a.conflicts.push_back(PackageName{"editor-b"});
    RepositoryVersion av;
    av.version = PackageVersion{"1.0"};
    av.artifact = PackageArtifact{"editor-a-1.0.pkg.tar.zst", "memory://a", "x"};
    a.versions.push_back(av);

    RepositoryPackage b;
    b.name = PackageName{"editor-b"};
    b.conflicts.push_back(PackageName{"editor-a"});
    RepositoryVersion bv;
    bv.version = PackageVersion{"1.0"};
    bv.artifact = PackageArtifact{"editor-b-1.0.pkg.tar.zst", "memory://b", "y"};
    b.versions.push_back(bv);

    repo.packages.push_back(a);
    repo.packages.push_back(b);

    MemoryRepositoryBackend backend(std::move(repo));
    auto loaded = backend.loadRepository();

    expectPass("conflicting packages coexist in in-memory repo",
               loaded.packages.size() == 2);
    const auto* pa = findPackage(loaded, PackageName{"editor-a"});
    const auto* pb = findPackage(loaded, PackageName{"editor-b"});
    expectPass("conflict metadata preserved in-memory",
               pa && pb && pa->conflicts[0].value == "editor-b" &&
                   pb->conflicts[0].value == "editor-a");
}

void testOptionalDependsMetadata() {
    // Optional dependencies are parsed from the manifest into repository
    // metadata but must not affect dependency resolution (metadata only).
    const char* toml =
        "format_version = 1\n"
        "name = \"app\"\n"
        "version = \"1.0.0\"\n"
        "architecture = \"AMD64\"\n"
        "depends = [\"libfoo\"]\n"
        "[[optional_depends]]\n"
        "package = \"gtk4\"\n"
        "description = \"GUI support\"\n"
        "[[optional_depends]]\n"
        "package = \"qt6\"\n"
        "description = \"Qt frontend\"\n";
    auto meta = meow::package::parsePackageManifest(toml);
    expectPass("optional_depends parsed into metadata",
               meta.optionalDependencies.size() == 2 &&
               meta.optionalDependencies[0].package.value == "gtk4" &&
               meta.optionalDependencies[0].description == "GUI support" &&
               meta.optionalDependencies[1].package.value == "qt6" &&
               meta.optionalDependencies[1].description == "Qt frontend");
    // Sanity: required depends still parsed alongside optional ones.
    expectPass("depends still parsed alongside optional_depends",
               meta.dependencies.value.size() == 1 &&
               meta.dependencies.value[0].name.value == "libfoo");
}

}  // namespace

int main() {
    std::cout << "MemoryRepositoryBackend unit tests\n";
    testLoadAndResolve();
    testArtifactFetch();
    testConflictingPackages();
    testOptionalDependsMetadata();
    std::cout << (failures == 0 ? "all passed\n" : "FAILURES present\n");
    return failures == 0 ? 0 : 1;
}
