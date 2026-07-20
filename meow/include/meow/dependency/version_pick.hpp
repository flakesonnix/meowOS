#ifndef MEOWOS_DEPENDENCY_VERSION_PICK_H
#define MEOWOS_DEPENDENCY_VERSION_PICK_H

#include <optional>

#include <meow/types/types.hpp>
#include <meow/repository/repository.hpp>
#include <meow/repository/version.hpp>

namespace meow::dependency {
namespace detail {

// Highest published version of a package (matches resolveAndStage's
// versions.back() choice) without downloading any artifact. Shared by the
// resolver backends so version selection stays identical across engines.
inline std::optional<types::PackageVersion> pickVersion(
    const repository::RepositoryPackage& pkg) {
    const types::PackageVersion* best = nullptr;
    for (const auto& rv : pkg.versions) {
        if (!best || repository::compareVersions(rv.version, *best) > 0)
            best = &rv.version;
    }
    if (!best) return std::nullopt;
    return *best;
}

}  // namespace detail
}  // namespace meow::dependency

#endif  // MEOWOS_DEPENDENCY_VERSION_PICK_H
