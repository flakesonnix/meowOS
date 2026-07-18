#ifndef MEOWOS_DEPENDENCY_RESOLVER_H
#define MEOWOS_DEPENDENCY_RESOLVER_H

#include <vector>

#include <meow/types/types.hpp>
#include <meow/package/package.hpp>
#include <meow/repository/repository.hpp>
#include <meow/database/database.hpp>

namespace meow::dependency {
    struct DependencyTree {
        std::vector<types::PackageName> packages;
    };

    DependencyTree resolveDependencies(
        const repository::Repository& repo,
        const package::PackageMetadata& package,
        database::Database& db
    );
}

#endif //MEOWOS_DEPENDENCY_RESOLVER_H
