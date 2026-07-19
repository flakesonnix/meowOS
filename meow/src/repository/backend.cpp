#include "meow/repository/backend.hpp"

#include <meow/error/error.hpp>
#include <meow/crypto/signature.hpp>
#include <meow/crypto/keystore.hpp>
#include <meow/format/version.hpp>
#include <meow/download/downloader.hpp>
#include <meow/log/logger.hpp>
#include <meow/package/package.hpp>
#include <meow/repository/repository.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <toml++/toml.hpp>

namespace meow::repository {

namespace fs = std::filesystem;

namespace {

fs::path cacheRoot() {
    const char* home = std::getenv("HOME");
    if (!home)
        throw error::MeowError(error::ErrorCode::Internal, "HOME not set");
    return fs::path(home) / ".cache" / "meow" / "repos";
}

fs::path cacheDirFor(const std::string& id) { return cacheRoot() / id; }

void atomicWrite(const fs::path& dest, const std::string& content) {
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

std::string readFileForCache(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void refreshRepoCache(const std::string& id, const fs::path& metaPath) {
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

std::optional<std::chrono::sys_seconds> parseRfc3339Z(const std::string& s) {
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

void validateRepoId(const std::string& id) {
    if (id.empty()) {
        throw error::MeowError(error::ErrorCode::InvalidRepository,
                               "repository_id is required");
    }
    for (char c : id) {
        bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '.' ||
                  c == '_' || c == '-';
        if (!ok) {
            throw error::MeowError(error::ErrorCode::InvalidRepository,
                                   "repository_id contains invalid characters: " +
                                       id);
        }
    }
}

void checkRepoExpiry(const std::string& repoName,
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

fs::path resolveLocalPath(const std::string& url) {
    if (url.starts_with("file://")) {
        return fs::path(url.substr(7));
    }
    return fs::absolute(fs::path(url));
}

void verifyRepoSig(const fs::path& repoMetaPath, const fs::path& cacheDir) {
    auto sigPath = cacheDir / "repository.toml.sig";

    if (!fs::exists(sigPath) &&
        fs::exists(repoMetaPath.parent_path() / "repository.toml.sig")) {
        sigPath = repoMetaPath.parent_path() / "repository.toml.sig";
    }

    if (!fs::exists(sigPath)) {
        log::log(log::LogLevel::Warning,
                 "repository not signed, skipping verification");
        return;
    }

    auto sig = crypto::loadSignature(sigPath);
    if (sig.keyId.empty()) {
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

std::vector<RepositoryPackage> scanByNameDir(const fs::path& byNameDir) {
    std::vector<RepositoryPackage> packages;

    for (const auto& shardDir : fs::directory_iterator(byNameDir)) {
        if (!shardDir.is_directory()) continue;

        for (const auto& pkgDir : fs::directory_iterator(shardDir.path())) {
            if (!pkgDir.is_directory()) continue;

            RepositoryPackage pkg;
            pkg.name = types::PackageName{pkgDir.path().filename().string()};

            auto pkgMetaPath = pkgDir.path() / "package.toml";
            if (fs::exists(pkgMetaPath)) {
                auto pkgTbl = toml::parse_file(pkgMetaPath.string());
                auto fmtVer = pkgTbl["format_version"].value_or(1);
                format::requireVersion("package metadata", fmtVer,
                                       format::CurrentPackageFormat);
                try {
                    if (auto desc = pkgTbl["description"].value<std::string>()) {
                        pkg.description = types::Description{*desc};
                    }
                    if (auto* arr = pkgTbl["provides"].as_array()) {
                        for (auto&& node : *arr) {
                            if (auto val = node.value<std::string>()) {
                                pkg.provides.push_back(types::PackageName{*val});
                            }
                        }
                    }
                    if (auto* arr = pkgTbl["conflicts"].as_array()) {
                        for (auto&& node : *arr) {
                            if (auto val = node.value<std::string>()) {
                                pkg.conflicts.push_back(types::PackageName{*val});
                            }
                        }
                    }
                    auto readNames = [&](const toml::array* arr) {
                        if (!arr) return;
                        for (auto&& node : *arr) {
                            if (auto val = node.value<std::string>()) {
                                pkg.depends.push_back(types::PackageName{*val});
                            }
                        }
                    };
                    readNames(pkgTbl["depends"].as_array());
                    if (auto* meta = pkgTbl["metadata"].as_table()) {
                        readNames((*meta)["depends"].as_array());
                    }
                } catch (...) {
                    log::log(log::LogLevel::Warning,
                             "failed to parse " + pkgMetaPath.string());
                }
            }

            auto versionsDir = pkgDir.path() / "versions";
            if (fs::is_directory(versionsDir)) {
                for (const auto& entry :
                     fs::directory_iterator(versionsDir)) {
                    if (!entry.is_regular_file()) continue;
                    if (entry.path().extension() != ".toml") continue;

                    try {
                        auto tbl = toml::parse_file(entry.path().string());
                        RepositoryVersion rv;
                        rv.version = types::PackageVersion{
                            entry.path().stem().string()};

                        if (auto* art = tbl["artifact"].as_table()) {
                            rv.artifact.filename =
                                (*art)["filename"].value_or("");
                            rv.artifact.url = (*art)["url"].value_or("");
                            rv.artifact.sha256 = (*art)["sha256"].value_or("");
                        }

                        pkg.versions.push_back(std::move(rv));
                    } catch (...) {
                        log::log(log::LogLevel::Warning,
                                 "failed to parse " + entry.path().string());
                    }
                }
            }

            std::sort(pkg.versions.begin(), pkg.versions.end(),
                      [](const RepositoryVersion& a,
                         const RepositoryVersion& b) {
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
    auto dir =
        fs::path(home) / ".cache" / "meow" / "repos" / safe;
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
        auto tbl = toml::parse_file(repoMetaPath.string());
        auto fmtVer = tbl["format_version"].value_or(1);
        format::requireVersion("repository", fmtVer,
                               format::CurrentRepositoryFormat);
        repo.name = tbl["name"].value_or("unnamed");
        repo.id = tbl["repository_id"].value_or("");
        if (auto g = tbl["generated"].value<std::string>())
            repo.generated = *g;
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

    refreshRepoCache(repo.id, repoMetaPath);

    auto byNameDir = root / "by-name";
    if (fs::is_directory(byNameDir)) {
        repo.packages = scanByNameDir(byNameDir);
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
    if (url.starts_with("http://") || url.starts_with("https://"))
        throw error::MeowError(
            error::ErrorCode::RepositoryNotFound,
            "remote repositories not yet implemented: " + url);
    return std::make_unique<FilesystemRepositoryBackend>(url);
}

}  // namespace meow::repository
