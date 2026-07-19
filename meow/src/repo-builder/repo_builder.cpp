#include <meow/repo-builder/repo_builder.hpp>
#include <meow/package/parser.hpp>
#include <meow/package/package.hpp>
#include <meow/archive/archive.hpp>
#include <meow/download/downloader.hpp>
#include <meow/error/error.hpp>
#include <meow/log/logger.hpp>
#include <meow/crypto/signature.hpp>
#include <toml++/toml.hpp>
#include <fstream>
#include <sstream>
#include <set>
#include <chrono>
#include <iomanip>

namespace meow::repo {
    namespace {
        std::string shardFor(const std::string& name) {
            if (name.size() < 2) return name + "_";
            std::string s;
            s += static_cast<char>(std::tolower(name[0]));
            s += static_cast<char>(std::tolower(name[1]));
            return s;
        }

        std::string readFileContent(const std::filesystem::path& path) {
            std::ifstream f(path, std::ios::binary);
            if (!f) return "";
            std::ostringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }

        void writeFileContent(const std::filesystem::path& path, const std::string& content) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream f(path);
            f << content;
        }

        std::string pkgMetadataToml(const package::PackageMetadata& meta, const std::string& sha256,
                                      const std::filesystem::path& archivePath) {
            std::ostringstream ss;
            ss << "format_version = 1\n";
            ss << "[metadata]\n";
            ss << "name = \"" << meta.name.value << "\"\n";
            ss << "version = \"" << meta.version.value << "\"\n";
            auto arch = meta.architecture == types::CpuArch::AMD64 ? "AMD64" : "AARCH64";
            ss << "architecture = \"" << arch << "\"\n";
            ss << "description = \"" << meta.description.value << "\"\n";

            if (!meta.dependencies.value.empty()) {
                ss << "depends = [";
                bool first = true;
                for (const auto& d : meta.dependencies.value) {
                    if (!first) ss << ", ";
                    first = false;
                    ss << "\"" << d.name.value;
                    for (const auto& c : d.constraints) {
                        ss << c.op << c.version.value;
                    }
                    ss << "\"";
                }
                ss << "]\n";
            }
            if (!meta.conflicts.value.empty()) {
                ss << "conflicts = [";
                bool first = true;
                for (const auto& c : meta.conflicts.value) {
                    if (!first) ss << ", ";
                    first = false;
                    ss << "\"" << c.name.value << "\"";
                }
                ss << "]\n";
            }
            if (!meta.provides.value.empty()) {
                ss << "provides = [";
                bool first = true;
                for (const auto& p : meta.provides.value) {
                    if (!first) ss << ", ";
                    first = false;
                    ss << "\"" << p.name.value << "\"";
                }
                ss << "]\n";
            }

            return ss.str();
        }

        std::string versionToml(const std::string& version, const std::string& sha256,
                                 const std::filesystem::path& archivePath) {
            std::ostringstream ss;
            ss << "[artifact]\n";
            ss << "filename = \"" << archivePath.filename().string() << "\"\n";
            ss << "url = \"file://" << std::filesystem::absolute(archivePath).string() << "\"\n";
            ss << "sha256 = \"" << sha256 << "\"\n";
            return ss.str();
        }
    }

    void repoAdd(const RepoBuildOptions& opts) {
        if (!opts.archivePath) {
            throw error::MeowError(error::ErrorCode::FileNotFound, "archive path required");
        }

        // Load the package
        auto archive = archive::openArchive(*opts.archivePath);
        auto content = archive::readFile(archive, "package.toml");
        auto metadata = package::parsePackageManifest(content);
        auto fileList = archive::listPackageContent(archive);

        auto sha256 = download::computeFileHash(*opts.archivePath);

        auto shard = shardFor(metadata.name.value);
        auto pkgDir = opts.repoDir / "by-name" / shard / metadata.name.value;
        auto versionsDir = pkgDir / "versions";
        std::filesystem::create_directories(versionsDir);

        // Write package.toml (repo metadata, partial, under [metadata])
        auto pkgMetaStr = pkgMetadataToml(metadata, sha256, *opts.archivePath);
        writeFileContent(pkgDir / "package.toml", pkgMetaStr);

        // Write version file
        auto verStr = versionToml(metadata.version.value, sha256, *opts.archivePath);
        writeFileContent(versionsDir / (metadata.version.value + ".toml"), verStr);

        // Copy archive to repo (opt)
        auto destArchive = opts.repoDir / "packages" / opts.archivePath->filename();
        if (!std::filesystem::exists(destArchive)) {
            std::filesystem::create_directories(destArchive.parent_path());
            std::filesystem::copy_file(*opts.archivePath, destArchive);
        }

        log::log(log::LogLevel::Info, "added " + metadata.name.value + " " + metadata.version.value + " to repo");
    }

    void repoRemove(const RepoBuildOptions& opts) {
        auto shard = shardFor(opts.pkgName);
        auto pkgDir = opts.repoDir / "by-name" / shard / opts.pkgName;

        if (!std::filesystem::exists(pkgDir)) {
            throw error::MeowError(error::ErrorCode::PackageNotFound,
                "package not found in repo: " + opts.pkgName);
        }

        std::filesystem::remove_all(pkgDir);
        log::log(log::LogLevel::Info, "removed " + opts.pkgName + " from repo");
    }

    std::string utcTimestamp(std::chrono::system_clock::time_point tp) {
        auto t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm{};
        gmtime_r(&t, &tm);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
        return std::string(buf);
    }

    void repoSync(const RepoBuildOptions& opts) {
        // Re-write repository.toml with correct mirror and name
        auto repoMetaPath = opts.repoDir / "repository.toml";
        std::string name = "local";
        if (std::filesystem::exists(repoMetaPath)) {
            try {
                auto tbl = toml::parse_file(repoMetaPath.string());
                name = tbl["name"].value_or("local");
            } catch (...) {}
        }

        auto now = std::chrono::system_clock::now();
        auto expires = now + std::chrono::hours(24 * 30);
        auto generatedStr = utcTimestamp(now);
        auto expiresStr = utcTimestamp(expires);

        std::ostringstream ss;
        ss << "format_version = 1\n";
        ss << "name = \"" << name << "\"\n\n";
        ss << "generated = \"" << generatedStr << "\"\n";
        ss << "expires = \"" << expiresStr << "\"\n\n";
        ss << "[[mirror]]\n";
        ss << "url = \"./repo\"\n";
        ss << "priority = 10\n";
        writeFileContent(repoMetaPath, ss.str());
        log::log(log::LogLevel::Info, "synced repository.toml");
    }

    void repoSigUpdate(const RepoBuildOptions& opts) {
        if (!opts.signKey) {
            throw error::MeowError(error::ErrorCode::FileNotFound, "signing key required");
        }

        auto repoMetaPath = opts.repoDir / "repository.toml";
        if (!std::filesystem::exists(repoMetaPath)) {
            throw error::MeowError(error::ErrorCode::FileNotFound,
                "repository.toml not found, run sync first");
        }

        auto sigPath = opts.repoDir / "repository.toml.sig";
        crypto::signFile(repoMetaPath, *opts.signKey, sigPath, opts.signKeyId);
        log::log(log::LogLevel::Info, "signed repository.toml");
    }
}
