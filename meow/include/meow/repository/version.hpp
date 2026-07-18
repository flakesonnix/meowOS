#ifndef MEOWOS_VERSION_H
#define MEOWOS_VERSION_H

#include <meow/types/types.hpp>
#include <meow/repository/repository.hpp>

namespace meow::repository {
    types::PackageVersion parseVersion(std::string_view version);
    int compareVersions(const types::PackageVersion& lhs, const types::PackageVersion& rhs);
    const types::PackageVersion* latestVersion(const RepositoryPackage& pkg);
}

#endif //MEOWOS_VERSION_H
