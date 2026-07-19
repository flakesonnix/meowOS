#include <meow/builder/builder.hpp>
#include <meow/package/parser.hpp>
#include <meow/error/error.hpp>
#include <meow/log/logger.hpp>
#include <meow/crypto/signature.hpp>
#include <meow/download/downloader.hpp>
#include <archive.h>
#include <archive_entry.h>
#include <fstream>
#include <sstream>
#include <set>

namespace meow::builder {
    namespace {
        std::string readFileContent(const std::filesystem::path& path) {
            std::ifstream f(path, std::ios::binary);
            if (!f) {
                throw error::MeowError(error::ErrorCode::FileNotFound,
                    "cannot read: " + path.string());
            }
            std::ostringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }

        void writeArchiveEntry(struct archive* a, const std::string& name,
                                const std::string& content) {
            struct archive_entry* entry = archive_entry_new();
            archive_entry_set_pathname(entry, name.c_str());
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0644);
            archive_entry_set_size(entry, static_cast<la_int64_t>(content.size()));
            archive_write_header(a, entry);
            archive_write_data(a, content.data(), content.size());
            archive_entry_free(entry);
        }

        void writeArchiveDir(struct archive* a, const std::string& name) {
            struct archive_entry* entry = archive_entry_new();
            archive_entry_set_pathname(entry, name.c_str());
            archive_entry_set_filetype(entry, AE_IFDIR);
            archive_entry_set_perm(entry, 0755);
            archive_write_header(a, entry);
            archive_entry_free(entry);
        }

        void addDirectoryToArchive(struct archive* a,
                                    const std::filesystem::path& diskDir,
                                    const std::string& archivePrefix) {
            if (!std::filesystem::is_directory(diskDir)) return;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(diskDir)) {
                auto rel = std::filesystem::relative(entry.path(), diskDir).string();
                auto archiveName = archivePrefix + "/" + rel;
                if (entry.is_directory()) {
                    writeArchiveDir(a, archiveName + "/");
                } else if (entry.is_regular_file()) {
                    writeArchiveEntry(a, archiveName, readFileContent(entry.path()));
                }
            }
        }

        void validateMetadata(const package::PackageMetadata& meta) {
            if (meta.name.value.empty()) {
                throw error::MeowError(error::ErrorCode::InvalidManifest,
                    "package.toml: name is required");
            }
            if (meta.version.value.empty()) {
                throw error::MeowError(error::ErrorCode::InvalidManifest,
                    "package.toml: version is required");
            }
        }
    }

    BuildResult buildPackage(const BuildOptions& opts) {
        BuildResult result;

        auto pkgTomlPath = opts.sourceDir / "package.toml";
        if (!std::filesystem::exists(pkgTomlPath)) {
            throw error::MeowError(error::ErrorCode::FileNotFound,
                "package.toml not found in " + opts.sourceDir.string());
        }

        auto content = readFileContent(pkgTomlPath);
        auto metadata = package::parsePackageManifest(content);
        validateMetadata(metadata);

        auto archName = metadata.architecture == types::CpuArch::AMD64 ? "amd64" : "aarch64";
        std::string pkgFilename = metadata.name.value + "-" + metadata.version.value + ".pkg.tar.zst";
        auto outPath = opts.outputDir / pkgFilename;

        std::filesystem::create_directories(opts.outputDir);

        struct archive* a = archive_write_new();
        archive_write_add_filter_zstd(a);
        archive_write_set_format_pax_restricted(a);
        archive_write_open_filename(a, outPath.c_str());

        // Write package.toml at root
        writeArchiveEntry(a, "package.toml", content);

        // Write files/ entries
        auto filesDir = opts.sourceDir / "files";
        if (std::filesystem::is_directory(filesDir)) {
            addDirectoryToArchive(a, filesDir, "files");
        }

        // Write scripts/ entries
        auto scriptsDir = opts.sourceDir / "scripts";
        if (std::filesystem::is_directory(scriptsDir)) {
            addDirectoryToArchive(a, scriptsDir, "scripts");
        }

        // Write metadata/ entries
        auto metaDir = opts.sourceDir / "metadata";
        if (std::filesystem::is_directory(metaDir)) {
            addDirectoryToArchive(a, metaDir, "metadata");
        }

        archive_write_close(a);
        archive_write_free(a);

        log::log(log::LogLevel::Info, "created " + outPath.string());

        result.success = true;
        result.archivePath = outPath;

        // Compute and update sha256 in the manifest
        auto sha256 = download::computeFileHash(outPath);
        log::log(log::LogLevel::Info, "sha256: " + sha256);

        // Optional signing
        if (opts.signKey) {
            auto sigPath = outPath;
            sigPath += ".sig";
            crypto::signFile(outPath, *opts.signKey, sigPath, opts.signKeyId);
            log::log(log::LogLevel::Info, "signed: " + sigPath.string());
            result.sigPath = sigPath;
        }

        return result;
    }
}
