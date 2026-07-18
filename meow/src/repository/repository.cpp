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

            RepositoryPackage pkg;
            pkg.name = types::PackageName{pkgDir.path().filename().string()};

            auto versionsDir = pkgDir.path() / "versions";
            if (std::filesystem::is_directory(versionsDir)) {
                for (const auto& verDir : std::filesystem::directory_iterator(versionsDir)) {
                    if (!verDir.is_directory()) continue;

                    RepositoryVersion rv;
                    rv.version = types::PackageVersion{verDir.path().filename().string()};

                    for (const auto& file : std::filesystem::directory_iterator(verDir.path())) {
                        if (file.is_regular_file()) {
                            rv.archive = file.path();
                            break;
                        }
                    }

                    if (!rv.archive.empty()) {
                        pkg.versions.push_back(std::move(rv));
                    }
                }
                std::sort(pkg.versions.begin(), pkg.versions.end(),
                    [](const RepositoryVersion& a, const RepositoryVersion& b) {
                        return a.version.value < b.version.value;
                    });
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

    std::vector<types::PackageVersion> listVersions(const RepositoryPackage& package) {
        std::vector<types::PackageVersion> versions;
        versions.reserve(package.versions.size());
        for (const auto& v : package.versions) {
            versions.push_back(v.version);
        }
        return versions;
    }
}
