#include <meow/repository/resolver.hpp>
#include <meow/repository/version.hpp>
#include <meow/download/downloader.hpp>
#include <cstdlib>
#include <stdexcept>

namespace meow::repository {
    namespace {
        std::filesystem::path cacheDir() {
            const char* home = std::getenv("HOME");
            if (!home) throw std::runtime_error("HOME not set");
            auto dir = std::filesystem::path(home) / ".cache" / "meow";
            std::filesystem::create_directories(dir);
            return dir;
        }

        std::filesystem::path downloadArtifact(const types::PackageArtifact& artifact) {
            auto dest = cacheDir() / artifact.filename;

            if (std::filesystem::exists(dest)) {
                if (download::verifyChecksum(dest, artifact.sha256)) {
                    return dest;
                }
                std::filesystem::remove(dest);
            }

            download::downloadFile(artifact.url, dest);

            if (!download::verifyChecksum(dest, artifact.sha256)) {
                std::filesystem::remove(dest);
                throw std::runtime_error("checksum mismatch for " + artifact.filename);
            }

            return dest;
        }
    }

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
                auto archive = downloadArtifact(rv.artifact);
                return package::loadPackage(archive);
            }
        }

        throw std::runtime_error("version not found: " + version.value + " for package: " + name.value);
    }
}
