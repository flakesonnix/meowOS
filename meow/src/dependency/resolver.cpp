#include <meow/dependency/resolver.hpp>
#include <meow/dependency/constraint.hpp>
#include <meow/repository/resolver.hpp>
#include <meow/repository/version.hpp>
#include <meow/error/error.hpp>
#include <set>

namespace meow::dependency {
    namespace {
        const repository::RepositoryPackage* findPackageWithProvides(
            const repository::Repository& repo,
            const types::PackageName& name
        ) {
            // first try exact match
            auto* pkg = repository::findPackage(repo, name);
            if (pkg) return pkg;

            // try provides
            for (const auto& rp : repo.packages) {
                for (const auto& p : rp.provides) {
                    if (p.value == name.value) {
                        return &rp;
                    }
                }
            }

            return nullptr;
        }

        void resolveDependency(
            const repository::Repository& repo,
            const types::Dependency& dep,
            database::Database& db,
            DependencyTree& tree,
            std::set<std::string>& visited,
            const lock::Lockfile* lock
        ) {
            if (visited.find(dep.name.value) != visited.end()) {
                throw error::MeowError(
                    error::ErrorCode::DependencyCycleDetected,
                    "cycle detected: " + dep.name.value
                );
            }

            if (database::isInstalled(db, dep.name)) {
                // check if installed version satisfies constraints
                auto installedVer = database::installedVersion(db, dep.name);
                if (installedVer && satisfiesConstraints(*installedVer, dep.constraints)) {
                    return;
                }
                if (installedVer) {
                    throw error::MeowError(
                        error::ErrorCode::DependencyNotFound,
                        "installed " + dep.name.value + " " + installedVer->value
                        + " does not satisfy constraints"
                    );
                }
            }

            visited.insert(dep.name.value);

            package::PackageFile pkg;
            if (lock) {
                pkg = repository::resolveLockedPackage(*lock, dep.name);
            } else {
                const auto* repoPkg = findPackageWithProvides(repo, dep.name);
                if (!repoPkg) {
                    throw error::MeowError(
                        error::ErrorCode::DependencyNotFound,
                        "package not found: " + dep.name.value
                    );
                }

                // find best version satisfying constraints
                const types::PackageVersion* bestVer = nullptr;
                for (const auto& rv : repoPkg->versions) {
                    if (satisfiesConstraints(rv.version, dep.constraints)) {
                        if (!bestVer || repository::compareVersions(rv.version, *bestVer) > 0) {
                            bestVer = &rv.version;
                        }
                    }
                }

                if (!bestVer) {
                    throw error::MeowError(
                        error::ErrorCode::VersionNotFound,
                        "no version of " + dep.name.value + " satisfies constraints"
                    );
                }

                pkg = repository::resolvePackage(repo, dep.name, *bestVer);
            }

            for (const auto& d : pkg.metadata.dependencies.value) {
                resolveDependency(repo, d, db, tree, visited, lock);
            }

            tree.packages.push_back(dep.name);
        }

        void checkConflicts(
            const repository::Repository& repo,
            const std::vector<types::PackageName>& toInstall,
            database::Database& db
        ) {
            std::set<std::string> installing;
            for (const auto& n : toInstall) {
                installing.insert(n.value);
            }

            // For simplicity, check conflict names against installed packages
            for (const auto& name : toInstall) {
                const auto* rp = repository::findPackage(repo, name);
                if (!rp) continue;

                for (const auto& conflict : rp->conflicts) {
                    if (installing.find(conflict.value) != installing.end()) {
                        throw error::MeowError(
                            error::ErrorCode::DependencyNotFound,
                            name.value + " conflicts with " + conflict.value
                        );
                    }
                    if (database::isInstalled(db, conflict)) {
                        throw error::MeowError(
                            error::ErrorCode::DependencyNotFound,
                            name.value + " conflicts with installed " + conflict.value
                        );
                    }
                }
            }
        }
    }

    std::vector<types::PackageName> findProvider(
        const repository::Repository& repo,
        const types::PackageName& virtualName
    ) {
        std::vector<types::PackageName> providers;
        for (const auto& rp : repo.packages) {
            for (const auto& p : rp.provides) {
                if (p.value == virtualName.value) {
                    providers.push_back(rp.name);
                }
            }
        }
        return providers;
    }

    DependencyTree resolveDependencies(
        const repository::Repository& repo,
        const package::PackageMetadata& package,
        database::Database& db,
        const lock::Lockfile* lock
    ) {
        DependencyTree tree;
        std::set<std::string> visited;

        types::Dependency self;
        self.name = package.name;
        self.constraints = {};
        resolveDependency(repo, self, db, tree, visited, lock);

        checkConflicts(repo, tree.packages, db);

        return tree;
    }
}
