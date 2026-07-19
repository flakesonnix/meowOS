#ifndef MEOWOS_DEPENDENCY_RESOLVER_H
#define MEOWOS_DEPENDENCY_RESOLVER_H

#include <vector>
#include <set>
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

    // A fully-specified install request, independent of any CLI flag parsing.
    // `packages` are the user-requested roots. Optional packages are promoted to
    // additional roots via `includeAllOptional` (every declared optional) or
    // `selectedOptional` (named ones only) before dependency resolution — the
    // resolver never learns about "optionalness".
    struct InstallRequest {
        std::vector<types::PackageName> packages;
        bool includeAllOptional = false;
        std::set<types::PackageName> selectedOptional;
    };

    // Expand an InstallRequest into the final set of root package names handed
    // to dependency resolution. Declared optional packages (from the requested
    // packages and their required dependency closure) are promoted to roots
    // according to `includeAllOptional` / `selectedOptional`. A name in
    // `selectedOptional` that is not declared optional by any such package is a
    // hard error. Throws on invalid selection.
    std::vector<types::PackageName> expandInstallRequest(
        const repository::Repository& repo,
        const InstallRequest& req);

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
