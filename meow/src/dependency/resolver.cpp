#include <meow/dependency/resolver.hpp>
#include <meow/repository/resolver.hpp>
#include <meow/error/error.hpp>
#include <set>

namespace meow::dependency {
    namespace {
        void resolveDependency(
            const repository::Repository& repo,
            const types::PackageName& name,
            database::Database& db,
            DependencyTree& tree,
            std::set<std::string>& visited
        ) {
            if (visited.find(name.value) != visited.end()) {
                throw error::MeowError(error::ErrorCode::DependencyCycleDetected, "cycle detected: " + name.value);
            }
            visited.insert(name.value);

            if (database::isInstalled(db, name)) {
                return;
            }

            auto pkg = repository::resolvePackage(repo, name);
            for (const auto& dep : pkg.metadata.dependencies.value) {
                resolveDependency(repo, dep, db, tree, visited);
            }

            tree.packages.push_back(name);
        }
    }

    DependencyTree resolveDependencies(
        const repository::Repository& repo,
        const package::PackageMetadata& package,
        database::Database& db
    ) {
        DependencyTree tree;
        std::set<std::string> visited;

        resolveDependency(repo, package.name, db, tree, visited);

        for (const auto& dep : package.dependencies.value) {
            resolveDependency(repo, dep, db, tree, visited);
        }

        return tree;
    }
}
