#include <meow/repository/repository.hpp>
#include <meow/repository/backend.hpp>
#include <meow/error/error.hpp>
#include <algorithm>
#include <functional>
#include <set>
#include <filesystem>

namespace meow::repository {

    Repository openRepository(const std::string& url) {
        return createBackend(url)->loadRepository();
    }

    const RepositoryPackage* findPackage(const Repository& repo, const types::PackageName& name) {
        for (const auto& pkg : repo.packages) {
            if (pkg.name.value == name.value) {
                return &pkg;
            }
        }
        return nullptr;
    }

    std::vector<types::PackageName> listPackages(const Repository& repo) {
        std::vector<types::PackageName> names;
        names.reserve(repo.packages.size());
        for (const auto& pkg : repo.packages) {
            names.push_back(pkg.name);
        }
        return names;
    }

    std::vector<types::PackageVersion> listVersions(const RepositoryPackage& package) {
        std::vector<types::PackageVersion> versions;
        versions.reserve(package.versions.size());
        for (const auto& v : package.versions) {
            versions.push_back(v.version);
        }
        return versions;
    }

    std::filesystem::path repositoryCacheRoot() {
        const char* home = std::getenv("HOME");
        if (!home)
            throw error::MeowError(error::ErrorCode::Internal, "HOME not set");
        return std::filesystem::path(home) / ".cache" / "meow" / "repos";
    }

    void clearRepositoryCache() {
        auto root = repositoryCacheRoot();
        if (std::filesystem::exists(root)) {
            std::filesystem::remove_all(root);
        }
    }

    std::vector<types::PackageName> resolveDependencyNames(
        const Repository& repo,
        const types::PackageName& top
    ) {
        std::vector<types::PackageName> order;
        std::set<std::string> visited;
        std::set<std::string> inStack;

        std::function<void(types::PackageName)> visit = [&](types::PackageName name) {
            if (visited.count(name.value)) return;
            if (inStack.count(name.value)) {
                throw error::MeowError(error::ErrorCode::DependencyCycleDetected,
                    "cycle detected: " + name.value);
            }
            inStack.insert(name.value);

            const auto* pkg = findPackage(repo, name);
            if (pkg) {
                for (const auto& dep : pkg->depends) {
                    visit(dep.name);
                }
            }

            inStack.erase(name.value);
            visited.insert(name.value);
            order.push_back(name);
        };

        visit(top);
        return order;
    }
}
