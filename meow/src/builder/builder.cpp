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
#include <vector>
#include <algorithm>
#include <ctime>

namespace meow::builder {
    namespace {
        // Fixed zstd settings so multi-core machines produce identical bytes.
        constexpr int kZstdLevel = 19;
        constexpr int kZstdThreads = 1;

        // Deterministic mtime used for every archive entry.
        long long resolveSourceDateEpoch(const package::BuildInfo& build) {
            if (build.sourceDateEpoch) return *build.sourceDateEpoch;
            const char* env = std::getenv("SOURCE_DATE_EPOCH");
            if (env && *env) {
                try {
                    return std::stoll(env);
                } catch (...) {
                    // fall through to default
                }
            }
            // Stable fallback: never "now".
            return 0;
        }

        struct ArchiveEntry {
            std::string name;
            std::string content;
            bool executable{false};
            bool isDir{false};
            bool isSymlink{false};
            std::string symlinkTarget;
        };

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

        // Collect every file (and its parent dirs) under diskDir into entries,
        // keyed by their archive-relative path. Dirs and files are gathered
        // separately and re-sorted globally for a stable order.
        void collectDir(const std::filesystem::path& diskDir,
                        const std::string& prefix,
                        std::vector<ArchiveEntry>& dirs,
                        std::vector<ArchiveEntry>& files) {
            if (!std::filesystem::is_directory(diskDir)) return;
            for (const auto& e : std::filesystem::recursive_directory_iterator(diskDir)) {
                auto rel = e.path().lexically_relative(diskDir).string();
                auto archiveName = prefix + "/" + rel;
                if (e.is_symlink()) {
                    ArchiveEntry se;
                    se.name = archiveName;
                    se.isSymlink = true;
                    se.symlinkTarget = std::filesystem::read_symlink(e.path()).string();
                    files.push_back(std::move(se));
                } else if (e.is_directory()) {
                    ArchiveEntry de;
                    de.name = archiveName + "/";
                    de.isDir = true;
                    dirs.push_back(std::move(de));
                } else if (e.is_regular_file()) {
                    ArchiveEntry fe;
                    fe.name = archiveName;
                    fe.content = readFileContent(e.path());
                    fe.executable = (std::filesystem::status(e.path()).permissions()
                                     & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
                    files.push_back(std::move(fe));
                }
            }
        }

        void writeEntry(struct archive* a, const ArchiveEntry& e, long long mtime) {
            struct archive_entry* entry = archive_entry_new();
            archive_entry_set_pathname(entry, e.name.c_str());
            archive_entry_set_filetype(entry, e.isDir ? AE_IFDIR : AE_IFREG);
            archive_entry_set_perm(entry, e.isDir ? 0755 : (e.executable ? 0755 : 0644));

            // Deterministic ownership.
            archive_entry_set_uid(entry, 0);
            archive_entry_set_gid(entry, 0);
            archive_entry_set_uname(entry, "root");
            archive_entry_set_gname(entry, "root");

            // Deterministic timestamp (no sub-second / no "now").
            archive_entry_set_mtime(entry, static_cast<time_t>(mtime), 0);
            archive_entry_set_ctime(entry, static_cast<time_t>(mtime), 0);
            archive_entry_set_atime(entry, static_cast<time_t>(mtime), 0);

            if (e.isSymlink) {
                archive_entry_set_filetype(entry, AE_IFLNK);
                archive_entry_set_symlink(entry, e.symlinkTarget.c_str());
            } else if (e.isDir) {
                archive_entry_set_size(entry, 0);
            } else {
                archive_entry_set_size(entry, static_cast<la_int64_t>(e.content.size()));
            }
            archive_write_header(a, entry);
            if (!e.isDir && !e.isSymlink && !e.content.empty()) {
                archive_write_data(a, e.content.data(), e.content.size());
            }
            archive_entry_free(entry);
        }

        std::string buildJson(const package::BuildInfo& build, long long epoch) {
            std::ostringstream os;
            os << "{\n";
            os << "  \"format_version\": 1,\n";
            os << "  \"builder\": \"meow-build\",\n";
            os << "  \"reproducible\": " << (build.reproducible ? "true" : "false") << ",\n";
            os << "  \"source_date_epoch\": " << epoch << "\n";
            os << "}\n";
            return os.str();
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

        auto epoch = resolveSourceDateEpoch(metadata.build);

        auto archName = metadata.architecture == types::CpuArch::AMD64 ? "amd64" : "aarch64";
        std::string pkgFilename = metadata.name.value + "-" + metadata.version.value + ".pkg.tar.zst";
        auto outPath = opts.outputDir / pkgFilename;

        std::filesystem::create_directories(opts.outputDir);

        // Gather all entries first so we can sort them into a stable order.
        std::vector<ArchiveEntry> dirs;
        std::vector<ArchiveEntry> files;
        ArchiveEntry pkgToml;
        pkgToml.name = "package.toml";
        pkgToml.content = content;
        files.push_back(std::move(pkgToml));

        collectDir(opts.sourceDir / "files", "files", dirs, files);
        collectDir(opts.sourceDir / "scripts", "scripts", dirs, files);
        collectDir(opts.sourceDir / "metadata", "metadata", dirs, files);

        // Synthetic build metadata (always written, makes builds self-describing).
        ArchiveEntry buildInfo;
        buildInfo.name = "metadata/build.json";
        buildInfo.content = buildJson(metadata.build, epoch);
        files.push_back(std::move(buildInfo));

        std::sort(dirs.begin(), dirs.end(),
                  [](const ArchiveEntry& a, const ArchiveEntry& b) { return a.name < b.name; });
        std::sort(files.begin(), files.end(),
                  [](const ArchiveEntry& a, const ArchiveEntry& b) { return a.name < b.name; });

        struct archive* a = archive_write_new();
        archive_write_add_filter_zstd(a);
        archive_write_set_format_pax_restricted(a);
        // Pin compression so output bytes are stable across machines.
        archive_write_set_filter_option(a, "zstd", "compression-level", std::to_string(kZstdLevel).c_str());
        archive_write_set_filter_option(a, "zstd", "threads", std::to_string(kZstdThreads).c_str());
        archive_write_open_filename(a, outPath.c_str());

        for (const auto& d : dirs) writeEntry(a, d, epoch);
        for (const auto& f : files) writeEntry(a, f, epoch);

        archive_write_close(a);
        archive_write_free(a);

        log::log(log::LogLevel::Info, "created " + outPath.string());

        result.success = true;
        result.archivePath = outPath;

        auto sha256 = download::computeFileHash(outPath);
        log::log(log::LogLevel::Info, "sha256: " + sha256);

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
