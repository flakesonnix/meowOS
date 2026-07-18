#include <meow/repository/repository.hpp>
#include <algorithm>
#include <stdexcept>

namespace meow::repository {
    Repository loadRepository(const std::filesystem::path& root) {
        if (!std::filesystem::is_directory(root)) {
            throw std::runtime_error("repository not found: " + root.string());
        }

        Repository repo;
        repo.root = root;

        for (const auto& pkgDir : std::filesystem::directory_iterator(root)) {
            if (!pkgDir.is_directory()) continue;

            types::PackageName name{pkgDir.path().filename().string()};

            RepositoryPackage pkg;
            pkg.name = name;

            auto versionsDir = pkgDir.path() / "versions";
            if (std::filesystem::is_directory(versionsDir)) {
                for (const auto& verDir : std::filesystem::directory_iterator(versionsDir)) {
                    if (!verDir.is_directory()) continue;
                    pkg.versions.push_back(verDir.path().filename());
                }
                std::sort(pkg.versions.begin(), pkg.versions.end());
            }

            repo.packages.push_back(std::move(pkg));
        }

        return repo;
    }

    const RepositoryPackage* findPackage(const Repository& repo, const types::PackageName& name) {
        for (const auto& pkg : repo.packages) {
            if (pkg.name.value == name.value) {
                return &pkg;
            }
        }
        return nullptr;
    }

    std::vector<types::PackageName> listPackages(const Repository& repo) {
        std::vector<types::PackageName> names;
        names.reserve(repo.packages.size());
        for (const auto& pkg : repo.packages) {
            names.push_back(pkg.name);
        }
        return names;
    }

    std::vector<std::filesystem::path> listVersions(const RepositoryPackage& package) {
        return package.versions;
    }
}
