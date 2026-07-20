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

        // Resolve a single dependency recursively. When `diags` is non-null,
        // failures are recorded there and the function returns without throwing
        // (so the caller can keep collecting other diagnostics). When `diags`
        // is null, failures throw as before (the install path).
        void resolveDependency(
            const repository::Repository& repo,
            const types::Dependency& dep,
            database::Database& db,
            DependencyTree& tree,
            std::set<std::string>& visited,
            const lock::Lockfile* lock,
            std::vector<ResolveDiagnostic>* diags
        ) {
            if (visited.find(dep.name.value) != visited.end()) {
                if (diags) {
                    ResolveDiagnostic d;
                    d.kind = ResolveDiagnostic::Kind::Cycle;
                    d.package = dep.name;
                    d.message = "cycle detected: " + dep.name.value;
                    diags->push_back(std::move(d));
                    return;
                }
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
                    if (diags) {
                        ResolveDiagnostic d;
                        d.kind = ResolveDiagnostic::Kind::VersionConflict;
                        d.package = dep.name;
                        d.message = "installed " + dep.name.value + " " +
                                    installedVer->value + " does not satisfy constraints";
                        for (const auto& c : dep.constraints) {
                            if (!d.requiredVersion.empty()) d.requiredVersion += ",";
                            d.requiredVersion += c.op + c.version.value;
                        }
                        d.availableVersion = installedVer->value;
                        diags->push_back(std::move(d));
                        return;
                    }
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
                    if (diags) {
                        // Distinguish a virtual name with no provider from a
                        // concrete package that simply does not exist.
                        ResolveDiagnostic d;
                        bool hasProvider = !findProvider(repo, dep.name).empty();
                        d.kind = hasProvider ? ResolveDiagnostic::Kind::MissingPackage
                                             : ResolveDiagnostic::Kind::MissingProvider;
                        d.package = dep.name;
                        d.message = hasProvider
                            ? "package not found: " + dep.name.value
                            : "no provider found for: " + dep.name.value;
                        diags->push_back(std::move(d));
                        return;
                    }
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
                    if (diags) {
                        ResolveDiagnostic d;
                        d.kind = ResolveDiagnostic::Kind::VersionConflict;
                        d.package = dep.name;
                        d.message = "no version of " + dep.name.value + " satisfies constraints";
                        for (const auto& c : dep.constraints) {
                            if (!d.requiredVersion.empty()) d.requiredVersion += ",";
                            d.requiredVersion += c.op + c.version.value;
                        }
                        // report the highest available version as "available"
                        const types::PackageVersion* highest = nullptr;
                        for (const auto& rv : repoPkg->versions) {
                            if (!highest || repository::compareVersions(rv.version, *highest) > 0)
                                highest = &rv.version;
                        }
                        if (highest) d.availableVersion = highest->value;
                        diags->push_back(std::move(d));
                        return;
                    }
                    throw error::MeowError(
                        error::ErrorCode::VersionNotFound,
                        "no version of " + dep.name.value + " satisfies constraints"
                    );
                }

                pkg = repository::resolvePackage(repo, dep.name, *bestVer);
            }

            for (const auto& d : pkg.metadata.dependencies.value) {
                resolveDependency(repo, d, db, tree, visited, lock, diags);
            }

            tree.packages.push_back(dep.name);
        }

        void checkConflicts(
            const repository::Repository& repo,
            const std::vector<types::PackageName>& toInstall,
            database::Database& db,
            std::vector<ResolveDiagnostic>* diags
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
                        if (diags) {
                            ResolveDiagnostic d;
                            d.kind = ResolveDiagnostic::Kind::PackageConflict;
                            d.package = name;
                            d.message = name.value + " conflicts with " + conflict.value;
                            diags->push_back(std::move(d));
                            continue;
                        }
                        throw error::MeowError(
                            error::ErrorCode::DependencyNotFound,
                            name.value + " conflicts with " + conflict.value
                        );
                    }
                    if (database::isInstalled(db, conflict)) {
                        if (diags) {
                            ResolveDiagnostic d;
                            d.kind = ResolveDiagnostic::Kind::PackageConflict;
                            d.package = name;
                            d.message = name.value + " conflicts with installed " + conflict.value;
                            diags->push_back(std::move(d));
                            continue;
                        }
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

    bool tryResolve(
        const repository::Repository& repo,
        const types::PackageName& top,
        database::Database& db,
        std::vector<ResolveDiagnostic>& diags,
        const lock::Lockfile* lock
    ) {
        DependencyTree tree;
        std::set<std::string> visited;
        types::Dependency self;
        self.name = top;
        self.constraints = {};
        resolveDependency(repo, self, db, tree, visited, lock, &diags);
        if (diags.empty()) {
            checkConflicts(repo, tree.packages, db, &diags);
        }
        return diags.empty();
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
        resolveDependency(repo, self, db, tree, visited, lock, nullptr);

        checkConflicts(repo, tree.packages, db, nullptr);

        return tree;
    }

    std::vector<types::PackageName> expandInstallRequest(
        const repository::Repository& repo,
        const InstallRequest& req) {
        // Gather every package in scope: the requested roots plus their
        // required dependency closure. Optional packages are declared on these.
        std::set<std::string> inScope;
        for (const auto& name : req.packages) {
            auto closure = repository::resolveDependencyNames(repo, name);
            for (const auto& c : closure) inScope.insert(c.value);
        }

        // Collect all declared optional dependencies across in-scope packages.
        std::set<std::string> declared;
        for (const auto& name : inScope) {
            const auto* pkg = repository::findPackage(repo, types::PackageName{name});
            if (!pkg) continue;
            for (const auto& od : pkg->optionalDepends) {
                declared.insert(od.package.value);
            }
        }

        // Determine which optionals become additional roots.
        std::set<std::string> chosen;
        if (req.includeAllOptional) {
            chosen = declared;
        } else {
            for (const auto& sel : req.selectedOptional) {
                if (declared.find(sel.value) == declared.end()) {
                    throw error::MeowError(
                        error::ErrorCode::DependencyNotFound,
                        "not an optional dependency: " + sel.value);
                }
                chosen.insert(sel.value);
            }
        }

        // Roots = requested packages + chosen optionals (no duplicates).
        std::vector<types::PackageName> roots = req.packages;
        for (const auto& c : chosen) {
            roots.push_back(types::PackageName{c});
        }
        return roots;
    }
}
