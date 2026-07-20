#include <meow/repository/package_index.hpp>
#include "meow/repository/backend.hpp"
#include "meow/repository/http_backend.hpp"
#include "meow/repository/memory_backend.hpp"
#include "backend_detail.hpp"

#include <meow/error/error.hpp>
#include <meow/download/downloader.hpp>
#include <meow/log/logger.hpp>
#include <meow/package/package.hpp>
#include <meow/repository/repository.hpp>

#include <filesystem>
#include <vector>

namespace meow::repository {

namespace fs = std::filesystem;
using namespace detail;

namespace {

std::vector<RepositoryPackage> scanByNameDir(
    const fs::path& byNameDir,
    const std::optional<PackageIndex>& idx) {
    std::vector<RepositoryPackage> packages;

    for (const auto& shardDir : fs::directory_iterator(byNameDir)) {
        if (!shardDir.is_directory()) continue;

        for (const auto& pkgDir : fs::directory_iterator(shardDir.path())) {
            if (!pkgDir.is_directory()) continue;

            auto pkgMetaPath = pkgDir.path() / "package.toml";
            if (!fs::exists(pkgMetaPath)) continue;

            auto pkg = parsePackageManifest(readFileForCache(pkgMetaPath),
                                            pkgDir.path().filename().string());

            auto versionsDir = pkgDir.path() / "versions";
            if (fs::is_directory(versionsDir)) {
                for (const auto& entry : fs::directory_iterator(versionsDir)) {
                    if (!entry.is_regular_file()) continue;
                    if (entry.path().extension() != ".toml") continue;
                    auto rv = [&]() -> std::optional<RepositoryVersion> {
                        try {
                            return parseVersionManifest(
                                readFileForCache(entry.path()),
                                entry.path().stem().string());
                        } catch (...) {
                            return std::nullopt;
                        }
                    }();
                    if (!rv) {
                        log::log(log::LogLevel::Warning,
                                 "failed to parse " + entry.path().string());
                        continue;
                    }
                    // v0.7: cross-check the loaded manifest against the
                    // signed package index and promote the trusted artifact
                    // hash. Compatibility mode (no index) is a no-op.
                    // Validation throws MeowError on trust disagreement and
                    // must propagate (not be swallowed).
                    validatePackageAgainstIndex(
                        idx, pkg.name.value, rv->version.value,
                        pkgMetaPath, entry.path());
                    auto trustedHash = lookupIndexArtifactHash(
                        idx, pkg.name.value, rv->version.value);
                    if (!trustedHash.empty()) {
                        rv->artifact.sha256 = trustedHash;
                    }
                    pkg.versions.push_back(std::move(*rv));
                }
            }

            std::sort(pkg.versions.begin(), pkg.versions.end(),
                      [](const RepositoryVersion& a, const RepositoryVersion& b) {
                          return a.version.value < b.version.value;
                      });

            packages.push_back(std::move(pkg));
        }
    }

    return packages;
}

fs::path repoCacheDir(const std::string& url) {
    const char* home = std::getenv("HOME");
    if (!home)
        throw error::MeowError(error::ErrorCode::Internal, "HOME not set");
    std::string safe;
    for (char c : url) {
        if (isalnum(c) || c == '/' || c == '.')
            safe += c;
        else
            safe += '_';
    }
    auto dir = fs::path(home) / ".cache" / "meow" / "repos" / safe;
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

}  // namespace

Repository FilesystemRepositoryBackend::loadRepository() {
    Repository repo;
    repo.cache = repoCacheDir(url_);

    auto root = resolveLocalPath(url_);

    if (!fs::is_directory(root)) {
        throw error::MeowError(error::ErrorCode::RepositoryNotFound,
                               "repository not found: " + root.string());
    }

    auto repoMetaPath = root / "repository.toml";
    if (fs::exists(repoMetaPath)) {
        toml::table tbl;
        try {
            tbl = toml::parse_file(repoMetaPath.string());
        } catch (const std::exception& e) {
            throw error::MeowError(error::ErrorCode::InvalidRepository,
                                   "malformed repository.toml: " +
                                       std::string(e.what()));
        }
        auto fmtVer = tbl["format_version"].value_or(1);
        format::requireVersion("repository", fmtVer,
                               format::CurrentRepositoryFormat);
        repo.name = tbl["name"].value_or("unnamed");
        repo.id = tbl["repository_id"].value_or("");
        if (auto g = tbl["generated"].value<std::string>()) repo.generated = *g;
        if (auto x = tbl["expires"].value<std::string>()) repo.expires = *x;

        if (auto* mirrorsArr = tbl["mirror"].as_array()) {
            for (const auto& elem : *mirrorsArr) {
                if (auto* mtbl = elem.as_table()) {
                    Mirror m;
                    m.url = (*mtbl)["url"].value_or("");
                    m.priority = (*mtbl)["priority"].value_or(10);
                    repo.mirrors.push_back(std::move(m));
                }
            }
        }
    }

    if (repo.mirrors.empty()) {
        Mirror m;
        m.url = "file://" + root.string();
        m.priority = 10;
        repo.mirrors.push_back(std::move(m));
    }

    verifyRepoSig(repoMetaPath, root);

    validateRepoId(repo.id);
    checkRepoExpiry(repo.name, repo.expires);

    // v0.7: load + verify the signed package index (when present). In
    // compatibility mode (no index) this returns nullopt and manifests are
    // trusted by transport, as before.
    repo.packageIndex = loadVerifiedPackageIndex(root);

    refreshRepoCache(repo.id, repoMetaPath);

    auto byNameDir = root / "by-name";
    if (fs::is_directory(byNameDir)) {
        repo.packages = scanByNameDir(byNameDir, repo.packageIndex);
        log::log(log::LogLevel::Debug,
                 std::to_string(repo.packages.size()) + " packages loaded");
    } else {
        log::log(log::LogLevel::Warning,
                 "by-name directory not found, creating empty repository");
    }

    return repo;
}

RepositoryPackage FilesystemRepositoryBackend::loadPackage(
    const types::PackageName& name) {
    Repository repo = loadRepository();
    for (const auto& p : repo.packages)
        if (p.name.value == name.value) return p;
    throw error::MeowError(error::ErrorCode::PackageNotFound,
                           "package not found: " + name.value);
}

package::PackageFile FilesystemRepositoryBackend::fetchArtifact(
    const types::PackageArtifact& artifact) {
    fs::path dest = repoCacheDir(url_) / artifact.filename;
    if (!fs::exists(dest))
        download::downloadFile(artifact.url, dest.string(),
                               download::DownloadOptions{});
    download::verifyChecksum(dest.string(), artifact.sha256);
    return package::loadPackage(dest.string());
}

std::unique_ptr<IRepositoryBackend> createBackend(const std::string& url) {
    if (url.starts_with("memory://"))
        throw error::MeowError(
            error::ErrorCode::Internal,
            "memory:// repositories must be constructed directly for tests");
    if (url.starts_with("http://") || url.starts_with("https://"))
        return std::make_unique<HttpRepositoryBackend>(url);
    return std::make_unique<FilesystemRepositoryBackend>(url);
}

}  // namespace meow::repository
