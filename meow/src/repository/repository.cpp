#include <meow/repository/repository.hpp>
#include <meow/error/error.hpp>
#include <meow/log/logger.hpp>
#include <toml++/toml.hpp>
#include <algorithm>

namespace meow::repository {
    Repository loadRepository(const std::filesystem::path& root) {
        if (!std::filesystem::is_directory(root)) {
            throw error::MeowError(error::ErrorCode::RepositoryNotFound, "repository not found: " + root.string());
        }

        Repository repo;
        repo.root = root;

        auto indexPath = root / "index.toml";
        if (std::filesystem::exists(indexPath)) {
            try {
                repo.index = loadIndex(indexPath);
                log::log(log::LogLevel::Debug, "loaded repository index");
            } catch (...) {
                log::log(log::LogLevel::Warning, "failed to load repository index, falling back to directory scan");
            }
        }

        for (const auto& pkgDir : std::filesystem::directory_iterator(root)) {
            if (!pkgDir.is_directory()) continue;
            if (pkgDir.path().filename() == "packages") {
                for (const auto& subDir : std::filesystem::directory_iterator(pkgDir.path())) {
                    if (!subDir.is_directory()) continue;

                    RepositoryPackage pkg;
                    pkg.name = types::PackageName{subDir.path().filename().string()};

                    auto versionsDir = subDir.path() / "versions";
                    if (std::filesystem::is_directory(versionsDir)) {
                        for (const auto& entry : std::filesystem::directory_iterator(versionsDir)) {
                            if (!entry.is_regular_file()) continue;
                            if (entry.path().extension() != ".toml") continue;

                            auto tbl = toml::parse_file(entry.path().string());
                            RepositoryVersion rv;
                            rv.version = types::PackageVersion{entry.path().stem().string()};

                            if (auto* art = tbl["artifact"].as_table()) {
                                rv.artifact.filename = (*art)["filename"].value_or("");
                                rv.artifact.url = (*art)["url"].value_or("");
                                rv.artifact.sha256 = (*art)["sha256"].value_or("");
                            }

                            pkg.versions.push_back(std::move(rv));
                        }
                    }

                    if (repo.index) {
                        const auto* entry = findIndexEntry(*repo.index, pkg.name);
                        if (entry) {
                            pkg.description = entry->description;
                        }
                    }

                    std::sort(pkg.versions.begin(), pkg.versions.end(),
                        [](const RepositoryVersion& a, const RepositoryVersion& b) {
                            return a.version.value < b.version.value;
                        });

                    repo.packages.push_back(std::move(pkg));
                }
                continue;
            }

            RepositoryPackage pkg;
            pkg.name = types::PackageName{pkgDir.path().filename().string()};

            auto versionsDir = pkgDir.path() / "versions";
            if (std::filesystem::is_directory(versionsDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(versionsDir)) {
                    if (!entry.is_regular_file()) continue;
                    if (entry.path().extension() != ".toml") continue;

                    auto tbl = toml::parse_file(entry.path().string());
                    RepositoryVersion rv;
                    rv.version = types::PackageVersion{entry.path().stem().string()};

                    if (auto* art = tbl["artifact"].as_table()) {
                        rv.artifact.filename = (*art)["filename"].value_or("");
                        rv.artifact.url = (*art)["url"].value_or("");
                        rv.artifact.sha256 = (*art)["sha256"].value_or("");
                    }

                    pkg.versions.push_back(std::move(rv));
                }
            }

            if (repo.index) {
                const auto* entry = findIndexEntry(*repo.index, pkg.name);
                if (entry) {
                    pkg.description = entry->description;
                }
            }

            std::sort(pkg.versions.begin(), pkg.versions.end(),
                [](const RepositoryVersion& a, const RepositoryVersion& b) {
                    return a.version.value < b.version.value;
                });

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
