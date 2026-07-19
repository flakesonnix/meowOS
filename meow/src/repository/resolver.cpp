#include <meow/repository/resolver.hpp>
#include <meow/repository/version.hpp>
#include <meow/download/downloader.hpp>
#include <meow/error/error.hpp>
#include <cstdlib>

namespace meow::repository {
    namespace {
        std::filesystem::path cacheDir() {
            const char* home = std::getenv("HOME");
            if (!home) throw error::MeowError(error::ErrorCode::Internal, "HOME not set");
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

            auto result = download::downloadFile(artifact.url, dest);

            if (!download::verifyChecksum(result.path, artifact.sha256)) {
                std::filesystem::remove(dest);
                throw error::MeowError(error::ErrorCode::ChecksumMismatch, "checksum mismatch for " + artifact.filename);
            }

            return dest;
        }
    }

    package::PackageFile resolvePackage(const Repository& repo, const types::PackageName& name) {
        const auto* pkg = findPackage(repo, name);
        if (!pkg) {
            throw error::MeowError(error::ErrorCode::PackageNotFound, "package not found: " + name.value);
        }

        const auto* ver = latestVersion(*pkg);
        if (!ver) {
            throw error::MeowError(error::ErrorCode::VersionNotFound, "no versions available for package: " + name.value);
        }

        return resolvePackage(repo, name, *ver);
    }

    package::PackageFile resolvePackage(const Repository& repo, const types::PackageName& name, const types::PackageVersion& version) {
        const auto* pkg = findPackage(repo, name);
        if (!pkg) {
            throw error::MeowError(error::ErrorCode::PackageNotFound, "package not found: " + name.value);
        }

        for (const auto& rv : pkg->versions) {
            if (rv.version.value == version.value) {
                auto archive = downloadArtifact(rv.artifact);
                return package::loadPackage(archive);
            }
        }

        throw error::MeowError(error::ErrorCode::VersionNotFound, "version not found: " + version.value + " for package: " + name.value);
    }

    package::PackageFile resolveLockedPackage(const lock::Lockfile& lock, const types::PackageName& name) {
        const auto* locked = lock::findLockedPackage(lock, name);
        if (!locked) {
            throw error::MeowError(
                error::ErrorCode::PackageNotFound,
                "package not found in lockfile: " + name.value
            );
        }

        auto archive = downloadArtifact(locked->artifact);
        return package::loadPackage(archive);
    }
}
