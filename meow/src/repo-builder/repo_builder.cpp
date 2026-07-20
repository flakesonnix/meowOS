#include <meow/repo-builder/repo_builder.hpp>
#include <meow/repository/package_index.hpp>
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

    std::string utcTimestamp(std::chrono::system_clock::time_point tp);

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

    // v0.7: (re)generate packages.toml from the by-name tree, binding
    // every package version to its manifest hash + artifact hash. The index is
    // signed separately by repoSigUpdate so it shares the repository key.
    void repoBuildIndex(const RepoBuildOptions& opts) {
        auto byNameDir = opts.repoDir / "by-name";
        if (!std::filesystem::is_directory(byNameDir)) {
            throw error::MeowError(error::ErrorCode::InvalidRepository,
                "no by-name directory; run add first");
        }

        std::ostringstream ss;
        ss << "format_version = 1\n";
        ss << "generated = \"" << utcTimestamp(std::chrono::system_clock::now()) << "\"\n\n";

        for (const auto& shardDir : std::filesystem::directory_iterator(byNameDir)) {
            if (!shardDir.is_directory()) continue;
            for (const auto& pkgDir : std::filesystem::directory_iterator(shardDir.path())) {
                if (!pkgDir.is_directory()) continue;
                auto pkgMetaPath = pkgDir.path() / "package.toml";
                if (!std::filesystem::exists(pkgMetaPath)) continue;

                auto versionsDir = pkgDir.path() / "versions";
                if (!std::filesystem::is_directory(versionsDir)) continue;

                for (const auto& entry : std::filesystem::directory_iterator(versionsDir)) {
                    if (!entry.is_regular_file()) continue;
                    if (entry.path().extension() != ".toml") continue;

                    auto version = entry.path().stem().string();
                    // Canonical manifest hash = sha256(package.toml || version toml),
                    // identical to the client-side computation in backend_detail.
                    auto manifestHash = repository::computeManifestHash(pkgMetaPath, entry.path());

                    // Artifact hash: read it back from the version manifest so the
                    // index reflects what is actually published.
                    std::string artifactHash;
                    std::uintmax_t size = 0;
                    try {
                        auto vt = toml::parse_file(entry.path().string());
                        if (auto* art = vt["artifact"].as_table()) {
                            artifactHash = (*art)["sha256"].value_or("");
                            std::string fn = (*art)["filename"].value_or("");
                            auto archive = opts.repoDir / "packages" / fn;
                            if (!fn.empty() && std::filesystem::exists(archive)) {
                                size = std::filesystem::file_size(archive);
                            }
                        }
                    } catch (...) {}

                    ss << "[[package]]\n";
                    ss << "name = \"" << pkgDir.path().filename().string() << "\"\n";
                    ss << "version = \"" << version << "\"\n";
                    ss << "manifest_hash = \"sha256:" << manifestHash << "\"\n";
                    ss << "artifact_hash = \"sha256:" << artifactHash << "\"\n";
                    ss << "size = " << size << "\n";
                    ss << "\n";
                }
            }
        }

        writeFileContent(opts.repoDir / "packages.toml", ss.str());
        log::log(log::LogLevel::Info, "built packages.toml index");
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
        ss << "name = \"" << name << "\"\n";
        ss << "repository_id = \"" << opts.repoId << "\"\n\n";
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

        // v0.7: regenerate the package index from the current by-name tree so
        // the signed index always matches what is published, then sign it with
        // the same key. Skipped only when there is nothing to index.
        if (std::filesystem::is_directory(opts.repoDir / "by-name")) {
            repoBuildIndex(opts);
        }

        // Sign the package index with the same key so the client can
        // authenticate every manifest/artifact hash.
        auto index = opts.repoDir / "packages.toml";
        if (std::filesystem::exists(index)) {
            auto indexSig = opts.repoDir / "packages.toml.sig";
            crypto::signFile(index, *opts.signKey, indexSig, opts.signKeyId);
            log::log(log::LogLevel::Info, "signed packages.toml");
        }
    }
}
