#ifndef MEOWOS_REPOSITORY_H
#define MEOWOS_REPOSITORY_H

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <meow/types/types.hpp>

namespace meow::repository {

struct Mirror {
    std::string url;
    int priority{10};
};

struct RepositoryVersion {
    types::PackageVersion version;
    types::PackageArtifact artifact;
};

struct RepositoryPackage {
    types::PackageName name;
    std::optional<types::Description> description;
    std::vector<types::PackageName> provides;
    std::vector<types::PackageName> conflicts;
    std::vector<types::Dependency> depends;
    std::vector<types::OptionalDependency> optionalDepends;
    std::vector<RepositoryVersion> versions;
};

struct Repository {
    std::string name;
    std::string id;
    std::vector<Mirror> mirrors;
    std::filesystem::path cache;
    std::vector<RepositoryPackage> packages;
    std::optional<std::string> generated;
    std::optional<std::string> expires;
};

Repository openRepository(const std::string& url);
const RepositoryPackage* findPackage(const Repository& repo, const types::PackageName& name);
std::vector<types::PackageName> listPackages(const Repository& repo);
std::vector<types::PackageVersion> listVersions(const RepositoryPackage& package);

// Resolve the dependency closure using repository metadata only (no
// downloads). Returns package names in install order (roots first).
std::vector<types::PackageName> resolveDependencyNames(
    const Repository& repo,
    const types::PackageName& top
);

std::filesystem::path repositoryCacheRoot();
void clearRepositoryCache();

}

#endif
