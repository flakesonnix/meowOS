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

    // One reason a resolution could not be satisfied. Produced by the resolver
    // at its internal decision points; the CLI (or any future API/JSON/GUI)
    // decides how to present it. The resolver never formats human strings.
    struct ResolveDiagnostic {
        enum class Kind {
            MissingPackage,
            VersionConflict,
            PackageConflict,
            MissingProvider,
            Cycle,
        };

        Kind kind = Kind::MissingPackage;
        types::PackageName package;
        std::string message;
        std::string requiredVersion;   // e.g. ">=2.0" for VersionConflict
        std::string availableVersion;  // best available version, if any
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

    // Non-throwing resolution for diagnostics (e.g. `meow why-not`). Returns
    // false when resolution cannot be satisfied and fills `diags` with the
    // reasons discovered at the resolver's own decision points. The install
    // path keeps using the throwing resolveDependencies().
    bool tryResolve(
        const repository::Repository& repo,
        const types::PackageName& top,
        database::Database& db,
        std::vector<ResolveDiagnostic>& diags,
        const lock::Lockfile* lock = nullptr
    );

    std::vector<types::PackageName> findProvider(
        const repository::Repository& repo,
        const types::PackageName& virtualName
    );
}

#endif
