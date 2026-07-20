#include <meow/repository/package_index.hpp>
#include "meow/repository/http_backend.hpp"
#include "backend_detail.hpp"

#include <meow/error/error.hpp>
#include <meow/download/downloader.hpp>
#include <meow/log/logger.hpp>
#include <meow/package/package.hpp>
#include <meow/repository/repository.hpp>

#include <filesystem>
#include <map>
#include <sstream>
#include <vector>

namespace meow::repository {

namespace fs = std::filesystem;
using namespace detail;

namespace {

fs::path httpCacheDir(const std::string& baseUrl) {
    const char* home = std::getenv("HOME");
    if (!home)
        throw error::MeowError(error::ErrorCode::Internal, "HOME not set");
    std::string safe;
    for (char c : baseUrl) {
        if (isalnum(c) || c == '/' || c == '.' || c == ':')
            safe += c;
        else
            safe += '_';
    }
    auto dir = fs::path(home) / ".cache" / "meow" / "repos-http" / safe;
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

}  // namespace

HttpRepositoryBackend::HttpRepositoryBackend(std::string baseUrl)
    : baseUrl_(std::move(baseUrl)) {
    // Normalize to a directory-style base (trailing slash) so relative
    // artifact paths resolve cleanly.
    if (!baseUrl_.empty() && baseUrl_.back() != '/') baseUrl_ += '/';
}

std::string HttpRepositoryBackend::absUrl(const std::string& relPath) const {
    if (!relPath.empty() && relPath.front() == '/') {
        std::string b = baseUrl_;
        b.pop_back();  // drop trailing slash
        return b + relPath;
    }
    return baseUrl_ + relPath;
}

Repository HttpRepositoryBackend::loadRepository() {
    Repository repo;
    auto cache = httpCacheDir(baseUrl_);
    repo.cache = cache;

    auto repoMetaPath = cache / "repository.toml";
    download::downloadFile(absUrl("repository.toml"), repoMetaPath);
    if (!fs::exists(repoMetaPath))
        throw error::MeowError(error::ErrorCode::RepositoryNotFound,
                               "cannot fetch repository.toml from " + baseUrl_);

    auto tbl = toml::parse_file(repoMetaPath.string());
    auto fmtVer = tbl["format_version"].value_or(1);
    format::requireVersion("repository", fmtVer, format::CurrentRepositoryFormat);
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

    if (repo.mirrors.empty()) {
        Mirror m;
        m.url = baseUrl_;
        m.priority = 10;
        repo.mirrors.push_back(std::move(m));
    }

    // Signature verification: download repository.toml.sig into the cache,
    // then verify exactly like the filesystem backend. A remote repo that
    // ships no signature is treated identically to an unsigned local repo.
    auto sigDest = cache / "repository.toml.sig";
    try {
        download::downloadFile(absUrl("repository.toml.sig"), sigDest);
    } catch (const error::MeowError&) {
        // Missing signature is tolerated (matches filesystem behavior).
    }
    verifyRepoSig(repoMetaPath, cache);
    validateRepoId(repo.id);
    checkRepoExpiry(repo.name, repo.expires);

    // v0.7: download + verify the signed package index when the server
    // ships one. Absent index is compatibility mode (manifests trusted by
    // transport). A missing signature or bad signature is fatal in strict
    // mode; otherwise warned and skipped.
    auto signedIndexDest = cache / "packages.toml";
    auto signedIndexSig = cache / "packages.toml.sig";
    // Drop any stale cached index so a repo that stopped publishing one does
    // not keep verifying against yesterday's file.
    { std::error_code rmec; fs::remove(signedIndexDest, rmec); fs::remove(signedIndexSig, rmec); }
    try {
        download::downloadFile(absUrl("packages.toml"), signedIndexDest);
    } catch (const error::MeowError&) {
        // Index absent on the server (e.g. 404). Compatibility mode unless
        // strict; loadVerifiedPackageIndex enforces the policy below.
    }
    if (fs::exists(signedIndexDest)) {
        try {
            download::downloadFile(absUrl("packages.toml.sig"), signedIndexSig);
        } catch (const error::MeowError&) {
            // signature missing; handled by loadVerifiedPackageIndex
        }
    }
    // Verify + load (or enforce strict policy on a missing index). Trust
    // failures (InvalidPackageIndex / MissingPackageIndex) propagate.
    repo.packageIndex = loadVerifiedPackageIndex(cache);

    refreshRepoCache(repo.id, repoMetaPath);

    // Enumerate packages. meow-server does not provide directory listings,
    // so an optional packages.index (newline-separated "name version" pairs)
    // lets the backend discover the by-name manifest layout. Without it the
    // repository loads but is empty until a specific package is requested.
    auto indexDest = cache / "packages.index";
    bool haveIndex = false;
    try {
        download::downloadFile(absUrl("packages.index"), indexDest);
        haveIndex = fs::exists(indexDest);
    } catch (const error::MeowError&) {
        haveIndex = false;
    }

    if (haveIndex) {
        std::map<std::string, std::vector<std::string>> versionsByName;
        {
            std::ifstream in(indexDest);
            std::string line;
            while (std::getline(in, line)) {
                std::istringstream iss(line);
                std::string name;
                if (!(iss >> name)) continue;
                std::string ver;
                while (iss >> ver) versionsByName[name].push_back(ver);
            }
        }

        for (const auto& [name, versions] : versionsByName) {
            std::string shard = name.substr(0, 2);
            auto pkgMetaPath = cache / ("pkg-" + name + ".toml");
            download::downloadFile(
                absUrl("by-name/" + shard + "/" + name + "/package.toml"),
                pkgMetaPath);
            auto pkg = parsePackageManifest(readFileForCache(pkgMetaPath), name);

            for (const auto& ver : versions) {
                auto verPath = cache / ("ver-" + name + "-" + ver + ".toml");
                download::downloadFile(
                    absUrl("by-name/" + shard + "/" + name + "/versions/" + ver +
                           ".toml"),
                    verPath);
                auto rv = parseVersionManifest(readFileForCache(verPath), ver);
                // v0.7: cross-check against the signed index and promote the
                // trusted artifact hash (compatibility mode is a no-op).
                validatePackageAgainstIndex(repo.packageIndex, name, ver,
                                           pkgMetaPath, verPath);
                auto trustedHash =
                    lookupIndexArtifactHash(repo.packageIndex, name, ver);
                if (!trustedHash.empty()) {
                    rv.artifact.sha256 = trustedHash;
                }
                rv.artifact.url =
                    resolveArtifactUrl(rv.artifact.url, baseUrl_);
                pkg.versions.push_back(std::move(rv));
            }

            std::sort(pkg.versions.begin(), pkg.versions.end(),
                      [](const RepositoryVersion& a, const RepositoryVersion& b) {
                          return a.version.value < b.version.value;
                      });

            repo.packages.push_back(std::move(pkg));
        }
    }

    log::log(log::LogLevel::Debug,
             std::to_string(repo.packages.size()) +
                 " packages loaded over HTTP");
    return repo;
}

RepositoryPackage HttpRepositoryBackend::loadPackage(
    const types::PackageName& name) {
    Repository repo = loadRepository();
    for (const auto& p : repo.packages)
        if (p.name.value == name.value) return p;
    throw error::MeowError(error::ErrorCode::PackageNotFound,
                           "package not found: " + name.value);
}

package::PackageFile HttpRepositoryBackend::fetchArtifact(
    const types::PackageArtifact& artifact) {
    auto cache = httpCacheDir(baseUrl_);
    fs::path dest = cache / artifact.filename;
    std::string url = resolveArtifactUrl(artifact.url, baseUrl_);
    if (!fs::exists(dest))
        download::downloadFile(url, dest.string(), download::DownloadOptions{});
    download::verifyChecksum(dest.string(), artifact.sha256);
    return package::loadPackage(dest.string());
}

}  // namespace meow::repository
