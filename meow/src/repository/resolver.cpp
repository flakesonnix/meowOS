#include <meow/repository/resolver.hpp>
#include <meow/repository/version.hpp>
#include <stdexcept>

namespace meow::repository {
    package::PackageFile resolvePackage(const Repository& repo, const types::PackageName& name) {
        const auto* pkg = findPackage(repo, name);
        if (!pkg) {
            throw std::runtime_error("package not found in repository: " + name.value);
        }

        const auto* ver = latestVersion(*pkg);
        if (!ver) {
            throw std::runtime_error("no versions available for package: " + name.value);
        }

        return resolvePackage(repo, name, *ver);
    }

    package::PackageFile resolvePackage(const Repository& repo, const types::PackageName& name, const types::PackageVersion& version) {
        const auto* pkg = findPackage(repo, name);
        if (!pkg) {
            throw std::runtime_error("package not found in repository: " + name.value);
        }

        for (const auto& rv : pkg->versions) {
            if (rv.version.value == version.value) {
                return package::loadPackage(rv.archive);
            }
        }

        throw std::runtime_error("version not found: " + version.value + " for package: " + name.value);
    }
}
