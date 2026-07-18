#ifndef MEOWOS_RESOLVER_H
#define MEOWOS_RESOLVER_H

#include <meow/repository/repository.hpp>
#include <meow/package/package.hpp>

namespace meow::repository {
    package::PackageFile resolvePackage(const Repository& repo, const types::PackageName& name);
    package::PackageFile resolvePackage(const Repository& repo, const types::PackageName& name, const types::PackageVersion& version);
}

#endif //MEOWOS_RESOLVER_H
