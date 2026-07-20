#ifndef MEOWOS_DEPENDENCY_IRESOLVER_H
#define MEOWOS_DEPENDENCY_IRESOLVER_H

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <meow/types/types.hpp>
#include <meow/package/package.hpp>
#include <meow/repository/repository.hpp>
#include <meow/database/database.hpp>
#include <meow/lock/lockfile.hpp>
#include <meow/config/config.hpp>
#include <meow/dependency/resolver.hpp>

namespace meow::dependency {

// A fully-normalized resolution request, free of any CLI flag parsing. Every
// entry point (install, group install, optional expansion) produces one of
// these before resolution, so a backend (legacy DFS or future SAT) never needs
// to know about command-line concepts.
struct ResolveRequest {
    std::vector<types::PackageName> roots;
    bool includeAllOptional = false;
    std::set<types::PackageName> selectedOptional;
    const lock::Lockfile* lock = nullptr;
    database::Database* db = nullptr;  // may be null for pure repository queries
};

// One package selected by a resolver, with enough to drive installation. The
// `version` is the chosen version; `isRoot` marks user-requested packages so
// callers can distinguish roots from pulled-in dependencies if needed.
struct ResolvedPackage {
    types::PackageName name;
    types::PackageVersion version;
    bool isRoot = false;
};

// A canonical resolution outcome. Independent of the backend: a DFS resolver
// and a SAT resolver must both produce this shape so the CLI and installer
// behave identically regardless of the engine in use.
struct ResolveResult {
    bool ok = false;
    std::vector<ResolvedPackage> packages;
    std::vector<ResolveDiagnostic> diagnostics;
};

// Backend-agnostic resolution interface. The CLI and installer depend only on
// this; the concrete implementation (LegacyResolver today, SatResolver later)
// is selected at the call site, never in the UI.
class IResolver {
public:
    virtual ~IResolver() = default;

    virtual ResolveResult resolve(const repository::Repository& repo,
                                  const ResolveRequest& req) = 0;
};

// Select a resolver backend from configuration. `Auto` currently maps to the
// legacy DFS resolver; flip this to Sat once the parity suite is trusted. The
// returned unique_ptr owns the backend; `Sat` uses an internal DPLL engine.
std::unique_ptr<IResolver> makeResolver(config::ResolverEngine engine);

}  // namespace meow::dependency

#endif  // MEOWOS_DEPENDENCY_IRESOLVER_H
