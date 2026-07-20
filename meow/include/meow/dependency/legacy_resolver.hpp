#ifndef MEOWOS_DEPENDENCY_LEGACY_RESOLVER_H
#define MEOWOS_DEPENDENCY_LEGACY_RESOLVER_H

#include <meow/dependency/iresolver.hpp>

namespace meow::dependency {

// Adapter wrapping the existing DFS resolver (resolver.cpp) behind the
// IResolver interface. It reuses expandInstallRequest for optional promotion
// and resolveDependencies for the closure, then derives chosen versions from
// the repository metadata (latest satisfying version) without downloading.
class LegacyResolver : public IResolver {
public:
    ResolveResult resolve(const repository::Repository& repo,
                          const ResolveRequest& req) override;
};

}  // namespace meow::dependency

#endif  // MEOWOS_DEPENDENCY_LEGACY_RESOLVER_H
