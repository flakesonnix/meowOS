#ifndef MEOWOS_DEPENDENCY_RESOLVER_H
#define MEOWOS_DEPENDENCY_RESOLVER_H

#include <vector>
#include <optional>
#include <string>

#include <meow/types/types.hpp>
#include <meow/package/package.hpp>
#include <meow/repository/repository.hpp>
#include <meow/database/database.hpp>
#include <meow/lock/lockfile.hpp>

namespace meow::dependency {
    struct DependencyTree {
        std::vector<types::PackageName> packages;
    };

    DependencyTree resolveDependencies(
        const repository::Repository& repo,
        const package::PackageMetadata& package,
        database::Database& db,
        const lock::Lockfile* lock = nullptr
    );

    std::vector<types::PackageName> findProvider(
        const repository::Repository& repo,
        const types::PackageName& virtualName
    );
}

#endif
