#ifndef MEOWOS_REPOSITORY_H
#define MEOWOS_REPOSITORY_H

#include <filesystem>
#include <optional>
#include <vector>

#include <meow/types/types.hpp>
#include <meow/repository/index.hpp>

namespace meow::repository {
    struct RepositoryVersion {
        types::PackageVersion version;
        types::PackageArtifact artifact;
    };

    struct RepositoryPackage {
        types::PackageName name;
        std::optional<types::Description> description;
        std::vector<RepositoryVersion> versions;
    };

    struct Repository {
        std::filesystem::path root;
        std::optional<RepositoryIndex> index;
        std::vector<RepositoryPackage> packages;
    };

    Repository loadRepository(const std::filesystem::path& root);
    const RepositoryPackage* findPackage(const Repository& repo, const types::PackageName& name);
    std::vector<types::PackageName> listPackages(const Repository& repo);
    std::vector<types::PackageVersion> listVersions(const RepositoryPackage& package);
}

#endif //MEOWOS_REPOSITORY_H
