#pragma once

// Internal helpers shared by the repository backends (filesystem + http).
// Not part of the public API.

#include <meow/error/error.hpp>
#include <meow/crypto/signature.hpp>
#include <meow/crypto/keystore.hpp>
#include <meow/dependency/constraint.hpp>
#include <meow/format/version.hpp>
#include <meow/log/logger.hpp>
#include <meow/package/package.hpp>
#include <meow/repository/security_policy.hpp>
#include <meow/repository/package_index.hpp>
#include <meow/repository/repository.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <toml++/toml.hpp>

namespace meow::repository::detail {

namespace fs = std::filesystem;

inline fs::path cacheRoot() {
    const char* home = std::getenv("HOME");
    if (!home)
        throw error::MeowError(error::ErrorCode::Internal, "HOME not set");
    return fs::path(home) / ".cache" / "meow" / "repos";
}

inline fs::path cacheDirFor(const std::string& id) {
    return cacheRoot() / id;
}

inline void atomicWrite(const fs::path& dest, const std::string& content) {
    auto tmp = dest;
    tmp += ".part";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            throw error::MeowError(error::ErrorCode::Internal,
                                   "cannot write cache file: " + tmp.string());
        out << content;
    }
    fs::rename(tmp, dest);
}

inline std::string readFileForCache(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline void refreshRepoCache(const std::string& id,
                             const fs::path& metaPath) {
    auto dir = cacheDirFor(id);
    std::error_code ec;
    fs::create_directories(dir, ec);

    auto sigPath = metaPath.parent_path() / "repository.toml.sig";
    atomicWrite(dir / "repository.toml", readFileForCache(metaPath));
    if (fs::exists(sigPath)) {
        atomicWrite(dir / "repository.toml.sig", readFileForCache(sigPath));
    }

    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);

    std::ostringstream meta;
    meta << "{\n";
    meta << "  \"etag\": \"\",\n";
    meta << "  \"last_checked\": \"" << buf << "\"\n";
    meta << "}\n";
    atomicWrite(dir / "metadata.json", meta.str());
}

inline std::optional<std::chrono::sys_seconds> parseRfc3339Z(
    const std::string& s) {
    if (s.empty()) return std::nullopt;
    std::tm tm{};
    std::istringstream iss(s);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (iss.fail()) return std::nullopt;
    tm.tm_isdst = 0;
    std::time_t t = timegm(&tm);
    if (t == -1) return std::nullopt;
    return std::chrono::sys_seconds{std::chrono::seconds{t}};
}

inline void validateRepoId(const std::string& id) {
    if (id.empty()) {
        throw error::MeowError(error::ErrorCode::InvalidRepository,
                               "repository_id is required");
    }
    for (char c : id) {
        bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '.' ||
                  c == '_' || c == '-';
        if (!ok) {
            throw error::MeowError(
                error::ErrorCode::InvalidRepository,
                "repository_id contains invalid characters: " + id);
        }
    }
}

inline void checkRepoExpiry(const std::string& repoName,
                            const std::optional<std::string>& expires) {
    if (!expires) return;
    auto exp = parseRfc3339Z(*expires);
    if (!exp) return;
    auto now = std::chrono::system_clock::now();
    if (std::chrono::time_point_cast<std::chrono::seconds>(now) >= *exp) {
        std::ostringstream msg;
        msg << "meow: repository metadata expired\n\n"
            << "  repository:\n      " << repoName << "\n\n"
            << "  expires:\n      " << *expires;
        throw error::MeowError(error::ErrorCode::RepositoryExpired, msg.str());
    }
}

inline fs::path resolveLocalPath(const std::string& url) {
    if (url.starts_with("file://")) {
        return fs::path(url.substr(7));
    }
    return fs::absolute(fs::path(url));
}

inline std::string stripHashPrefix(const std::string& h) {
    return h.starts_with("sha256:") ? h.substr(7) : h;
}

// Canonical manifest hash lives in package_index.hpp (meow::repository) so the
// repo-builder and every backend share one definition. Reachable unqualified
// from this nested namespace.

// Verify the signature over packages.toml using the existing Ed25519 trust
// chain (same trusted key store as repository.toml.sig). Returns false on
// missing/empty keyId or bad signature rather than throwing, so callers can
// map to the right error code.
inline bool verifyIndexSignature(const fs::path& index, const fs::path& sig) {
    auto s = crypto::loadSignature(sig);
    if (s.keyId.empty()) return false;
    auto key = crypto::loadTrustedKey(s.keyId);
    return crypto::verifyFile(index, sig, key.path);
}

// Load + verify the signed package index from `dir` (packages.toml +
// packages.toml.sig). Returns nullopt when the index is absent (compat
// fallback). Throws:
//   MissingPackageIndex - .sig missing and requirePackageIndex is set
//   InvalidPackageIndex - parse or signature verification failure
inline std::optional<PackageIndex> loadVerifiedPackageIndex(const fs::path& dir) {
    auto index = dir / "packages.toml";
    auto sig = dir / "packages.toml.sig";
    if (!fs::exists(index)) {
        // No index at all: legacy repo. Under strict mode this is rejected;
        // otherwise manifest verification falls back to transport trust.
        if (securityPolicy().requirePackageIndex) {
            throw error::MeowError(
                error::ErrorCode::MissingPackageIndex,
                "repository has no package index and require_package_index is set");
        }
        return std::nullopt;
    }
    if (!fs::exists(sig)) {
        if (securityPolicy().requirePackageIndex) {
            throw error::MeowError(
                error::ErrorCode::MissingPackageIndex,
                "package index is unsigned and require_package_index is set");
        }
        log::log(log::LogLevel::Warning,
                  "package index not signed, skipping verification");
        return parsePackageIndex(index);
    }
    if (!verifyIndexSignature(index, sig)) {
        throw error::MeowError(
            error::ErrorCode::InvalidPackageIndex,
            "package index signature invalid");
    }
    return parsePackageIndex(index);
}

inline const PackageIndexEntry* findIndexEntry(const PackageIndex& idx,
                                            const std::string& name,
                                            const std::string& version) {
    for (const auto& e : idx.packages) {
        if (e.name == name && e.version == version) return &e;
    }
    return nullptr;
}

// Validate a loaded package manifest + version against the signed index.
// When `idx` is present the manifest hash must match the signed entry and a
// corresponding entry must exist. Throws PackageIndexMismatch on any
// disagreement (missing entry, hash mismatch, unexpected version).
inline void validatePackageAgainstIndex(
    const std::optional<PackageIndex>& idx,
    const std::string& name,
    const std::string& version,
    const fs::path& pkgMetaPath,
    const fs::path& versionPath) {
    if (!idx) return;  // compatibility mode: trust transport
    const auto* entry = findIndexEntry(*idx, name, version);
    if (!entry) {
        throw error::MeowError(
            error::ErrorCode::PackageIndexMismatch,
            "package " + name + " " + version +
            " is not listed in the signed package index");
    }
    auto computed = computeManifestHash(pkgMetaPath, versionPath);
    if (stripHashPrefix(entry->manifestHash) != computed) {
        throw error::MeowError(
            error::ErrorCode::PackageIndexMismatch,
            "package " + name + " " + version +
            " manifest hash does not match the signed package index");
    }
}

// Return the trusted artifact hash for (name, version) from the signed index,
// or the empty string when no index entry exists (caller keeps its own value).
inline std::string lookupIndexArtifactHash(
    const std::optional<PackageIndex>& idx,
    const std::string& name,
    const std::string& version) {
    if (!idx) return "";
    const auto* entry = findIndexEntry(*idx, name, version);
    if (!entry) return "";
    return stripHashPrefix(entry->artifactHash);
}

inline void verifyRepoSig(const fs::path& repoMetaPath,
                          const fs::path& cacheDir) {
    auto sigPath = cacheDir / "repository.toml.sig";

    if (!fs::exists(sigPath) &&
        fs::exists(repoMetaPath.parent_path() / "repository.toml.sig")) {
        sigPath = repoMetaPath.parent_path() / "repository.toml.sig";
    }

    if (!fs::exists(sigPath)) {
        if (securityPolicy().requireRepositorySignature) {
            throw error::MeowError(
                error::ErrorCode::InvalidSignature,
                "repository is not signed and require_repository_signature is set");
        }
        log::log(log::LogLevel::Warning,
                 "repository not signed, skipping verification");
        return;
    }

    auto sig = crypto::loadSignature(sigPath);
    if (sig.keyId.empty()) {
        if (securityPolicy().requireRepositorySignature) {
            throw error::MeowError(
                error::ErrorCode::InvalidSignature,
                "repository signature has no keyId and "
                "require_repository_signature is set");
        }
        log::log(log::LogLevel::Warning,
                 "signature has no keyId, skipping verification");
        return;
    }

    auto key = crypto::loadTrustedKey(sig.keyId);
    if (crypto::verifyFile(repoMetaPath, sigPath, key.path)) {
        log::log(log::LogLevel::Info,
                 "repository signature verified (key: " + sig.keyId + ")");
    } else {
        throw error::MeowError(error::ErrorCode::InvalidSignature,
                               "repository signature invalid");
    }
}

// Parse a package-level manifest (by-name/<name>/package.toml) describing
// description/provides/conflicts/depends. Version artifacts are parsed
// separately via parseVersionManifest.
inline RepositoryPackage parsePackageManifest(const std::string& tomlText,
                                              const std::string& name) {
    RepositoryPackage pkg;
    pkg.name = types::PackageName{name};

    auto pkgTbl = toml::parse(tomlText);
    auto fmtVer = pkgTbl["format_version"].value_or(1);
    format::requireVersion("package metadata", fmtVer,
                           format::CurrentPackageFormat);
    try {
        if (auto desc = pkgTbl["description"].value<std::string>()) {
            pkg.description = types::Description{*desc};
        }
        if (auto* arr = pkgTbl["provides"].as_array()) {
            for (auto&& node : *arr) {
                if (auto val = node.value<std::string>())
                    pkg.provides.push_back(types::PackageName{*val});
            }
        }
        if (auto* arr = pkgTbl["conflicts"].as_array()) {
            for (auto&& node : *arr) {
                if (auto val = node.value<std::string>())
                    pkg.conflicts.push_back(types::PackageName{*val});
            }
        }
        auto readNames = [&](const toml::array* arr) {
            if (!arr) return;
            for (auto&& node : *arr) {
                if (auto val = node.value<std::string>())
                    pkg.depends.push_back(dependency::parseDependencyString(*val));
            }
        };
        readNames(pkgTbl["depends"].as_array());
        if (auto* meta = pkgTbl["metadata"].as_table()) {
            readNames((*meta)["depends"].as_array());
        }
        // Optional dependencies are metadata only (not yet resolver input).
        if (auto* optArr = pkgTbl["optional_depends"].as_array()) {
            for (auto&& node : *optArr) {
                if (auto* t = node.as_table()) {
                    types::OptionalDependency od;
                    if (auto pkg = (*t)["package"].value<std::string>())
                        od.package = types::PackageName{*pkg};
                    od.description = (*t)["description"].value_or("");
                    if (!od.package.value.empty())
                        pkg.optionalDepends.push_back(std::move(od));
                }
            }
        }
    } catch (...) {
        log::log(log::LogLevel::Warning, "failed to parse package " + name);
    }

    return pkg;
}

// Parse a version manifest (by-name/<name>/versions/<ver>.toml) describing a
// single [artifact].
inline RepositoryVersion parseVersionManifest(const std::string& tomlText,
                                              const std::string& version) {
    RepositoryVersion rv;
    rv.version = types::PackageVersion{version};

    auto tbl = toml::parse(tomlText);
    if (auto* art = tbl["artifact"].as_table()) {
        rv.artifact.filename = (*art)["filename"].value_or("");
        rv.artifact.url = (*art)["url"].value_or("");
        rv.artifact.sha256 = (*art)["sha256"].value_or("");
    }
    return rv;
}

// Resolve an artifact URL. Absolute URLs (http/https/file) are used as-is;
// relative URLs are resolved against the given base. This keeps repositories
// portable across hosts.
inline std::string resolveArtifactUrl(const std::string& raw,
                                      const std::string& baseUrl) {
    if (raw.starts_with("http://") || raw.starts_with("https://") ||
        raw.starts_with("file://"))
        return raw;
    if (raw.empty()) return raw;
    std::string base = baseUrl;
    if (!base.empty() && base.back() == '/') base.pop_back();
    if (!raw.empty() && raw.front() == '/') return base + raw;
    return base + "/" + raw;
}

}  // namespace meow::repository::detail
