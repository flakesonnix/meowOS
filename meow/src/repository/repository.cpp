#include <meow/repository/repository.hpp>
#include <meow/error/error.hpp>
#include <meow/log/logger.hpp>
#include <meow/crypto/signature.hpp>
#include <meow/crypto/keystore.hpp>
#include <meow/download/downloader.hpp>
#include <meow/format/version.hpp>
#include <toml++/toml.hpp>
#include <algorithm>
#include <cstdlib>
#include <set>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace meow::repository {
    namespace {
        std::filesystem::path repoCacheDir(const std::string& url) {
            const char* home = std::getenv("HOME");
            if (!home) throw error::MeowError(error::ErrorCode::Internal, "HOME not set");
            std::string safe;
            for (char c : url) {
                if (isalnum(c) || c == '/' || c == '.') safe += c;
                else safe += '_';
            }
            auto dir = std::filesystem::path(home) / ".cache" / "meow" / "repos" / safe;
            std::filesystem::create_directories(dir);
            return dir;
        }

        std::string fetchUrl(const std::string& base, const std::string& path) {
            if (base.back() == '/') return base + path;
            return base + "/" + path;
        }

        bool isHttpUrl(const std::string& url) {
            return url.starts_with("http://") || url.starts_with("https://");
        }

        bool isFilePath(const std::string& url) {
            if (url.starts_with("file://")) return true;
            if (!isHttpUrl(url) && url.find("://") == std::string::npos) return true;
            return false;
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

        void checkRepoExpiry(const std::string& repoName, const std::optional<std::string>& expires) {
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

        std::filesystem::path resolveLocalPath(const std::string& url) {
            if (url.starts_with("file://")) {
                return std::filesystem::path(url.substr(7));
            }
            return std::filesystem::absolute(std::filesystem::path(url));
        }

        void verifyRepoSig(const std::filesystem::path& repoMetaPath,
                           const std::filesystem::path& cacheDir) {
            auto sigPath = cacheDir / "repository.toml.sig";

            if (!std::filesystem::exists(sigPath) && std::filesystem::exists(repoMetaPath.parent_path() / "repository.toml.sig")) {
                sigPath = repoMetaPath.parent_path() / "repository.toml.sig";
            }

            if (!std::filesystem::exists(sigPath)) {
                log::log(log::LogLevel::Warning, "repository not signed, skipping verification");
                return;
            }

            auto sig = crypto::loadSignature(sigPath);
            if (sig.keyId.empty()) {
                log::log(log::LogLevel::Warning, "signature has no keyId, skipping verification");
                return;
            }

            try {
                auto key = crypto::loadTrustedKey(sig.keyId);
                if (crypto::verifyFile(repoMetaPath, sigPath, key.path)) {
                    log::log(log::LogLevel::Info, "repository signature verified (key: " + sig.keyId + ")");
                } else {
                    throw error::MeowError(
                        error::ErrorCode::InvalidSignature,
                        "repository signature invalid"
                    );
                }
            } catch (const error::MeowError& e) {
                if (e.code == error::ErrorCode::TrustedKeyNotFound) {
                    throw;
                }
                throw;
            }
        }

        std::vector<RepositoryPackage> scanByNameDir(const std::filesystem::path& byNameDir) {
            std::vector<RepositoryPackage> packages;

            for (const auto& shardDir : std::filesystem::directory_iterator(byNameDir)) {
                if (!shardDir.is_directory()) continue;

                for (const auto& pkgDir : std::filesystem::directory_iterator(shardDir.path())) {
                    if (!pkgDir.is_directory()) continue;

                    RepositoryPackage pkg;
                    pkg.name = types::PackageName{pkgDir.path().filename().string()};

                    auto pkgMetaPath = pkgDir.path() / "package.toml";
                    if (std::filesystem::exists(pkgMetaPath)) {
                        auto pkgTbl = toml::parse_file(pkgMetaPath.string());
                        auto fmtVer = pkgTbl["format_version"].value_or(1);
                        format::requireVersion("package metadata", fmtVer, format::CurrentPackageFormat);
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
                        } catch (...) {
                            log::log(log::LogLevel::Warning,
                                "failed to parse " + pkgMetaPath.string());
                        }
                    }

                    auto versionsDir = pkgDir.path() / "versions";
                    if (std::filesystem::is_directory(versionsDir)) {
                        for (const auto& entry : std::filesystem::directory_iterator(versionsDir)) {
                            if (!entry.is_regular_file()) continue;
                            if (entry.path().extension() != ".toml") continue;

                            try {
                                auto tbl = toml::parse_file(entry.path().string());
                                RepositoryVersion rv;
                                rv.version = types::PackageVersion{entry.path().stem().string()};

                                if (auto* art = tbl["artifact"].as_table()) {
                                    rv.artifact.filename = (*art)["filename"].value_or("");
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
                        [](const RepositoryVersion& a, const RepositoryVersion& b) {
                            return a.version.value < b.version.value;
                        });

                    packages.push_back(std::move(pkg));
                }
            }

            return packages;
        }
    }

    Repository openRepository(const std::string& url) {
        Repository repo;
        repo.cache = repoCacheDir(url);

        if (isFilePath(url)) {
            auto root = resolveLocalPath(url);

            if (!std::filesystem::is_directory(root)) {
                throw error::MeowError(error::ErrorCode::RepositoryNotFound, "repository not found: " + root.string());
            }

            auto repoMetaPath = root / "repository.toml";
            if (std::filesystem::exists(repoMetaPath)) {
                auto tbl = toml::parse_file(repoMetaPath.string());
                auto fmtVer = tbl["format_version"].value_or(1);
                format::requireVersion("repository", fmtVer, format::CurrentRepositoryFormat);
                repo.name = tbl["name"].value_or("unnamed");
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

            checkRepoExpiry(repo.name, repo.expires);

            auto byNameDir = root / "by-name";
            if (std::filesystem::is_directory(byNameDir)) {
                repo.packages = scanByNameDir(byNameDir);
                log::log(log::LogLevel::Debug, std::to_string(repo.packages.size()) + " packages loaded");
            } else {
                log::log(log::LogLevel::Warning, "by-name directory not found, creating empty repository");
            }
        } else {
            throw error::MeowError(error::ErrorCode::RepositoryNotFound, "remote repositories not yet implemented: " + url);
        }

        return repo;
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
}
