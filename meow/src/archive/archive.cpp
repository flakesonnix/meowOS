#include <meow/archive/archive.hpp>
#include <meow/error/error.hpp>
#include <archive.h>
#include <archive_entry.h>
#include <array>

namespace meow::archive {
    namespace {
        constexpr std::size_t blockSize = 10240;

        struct ArchiveHandle {
            struct archive* ptr;

            explicit ArchiveHandle(const std::filesystem::path& path) {
                ptr = archive_read_new();
                if (!ptr) {
                    throw error::MeowError(error::ErrorCode::ArchiveOpenFailed, "archive_read_new failed");
                }
                archive_read_support_format_all(ptr);
                archive_read_support_filter_all(ptr);

                if (archive_read_open_filename(ptr, path.c_str(), blockSize) != ARCHIVE_OK) {
                    std::string err = archive_error_string(ptr);
                    archive_read_free(ptr);
                    ptr = nullptr;
                    throw error::MeowError(error::ErrorCode::ArchiveOpenFailed, "cannot open archive: " + err);
                }
            }

            ~ArchiveHandle() {
                if (ptr) {
                    archive_read_free(ptr);
                }
            }

            ArchiveHandle(ArchiveHandle&& other) noexcept
                : ptr(other.ptr)
            {
                other.ptr = nullptr;
            }

            ArchiveHandle& operator=(ArchiveHandle&& other) noexcept {
                if (this != &other) {
                    if (ptr) {
                        archive_read_free(ptr);
                    }
                    ptr = other.ptr;
                    other.ptr = nullptr;
                }
                return *this;
            }

            ArchiveHandle(const ArchiveHandle&) = delete;
            ArchiveHandle& operator=(const ArchiveHandle&) = delete;
        };

        void skipData(struct archive* a) {
            if (archive_read_data_skip(a) != ARCHIVE_OK) {
                throw error::MeowError(error::ErrorCode::ArchiveInvalid, "error skipping archive data");
            }
        }

        const std::string filesPrefix = "files/";

        // libarchive security flags applied to every extraction. Combined with
        // the explicit pre-checks below they defend against the classic
        // package-archive attacks:
        //   SECURE_NODOTDOT       - reject entries containing ".."
        //   SECURE_SYMLINKS       - refuse to write through an existing symlink
        //   UNLINK                - remove an existing target (incl. symlink)
        //                           before writing, closing symlink-follow TOCTOU
        //
        // ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS is intentionally NOT set here:
        // we rewrite each entry's pathname to an absolute `destination / rel`,
        // which that flag would reject. Absolute *source* entries are instead
        // caught by ensureSafeEntry() before extraction.
        constexpr int secureExtractFlags =
            ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_TIME |
            ARCHIVE_EXTRACT_SECURE_NODOTDOT |
            ARCHIVE_EXTRACT_SECURE_SYMLINKS |
            ARCHIVE_EXTRACT_UNLINK;

        std::string norm(const char* raw) {
            std::string s = raw ? raw : "";
            if (s.starts_with("./")) s = s.substr(2);
            if (s.ends_with("/") && s.size() > 1) s.pop_back();
            return s;
        }

        // Reject a relative archive entry that would escape `destination` once
        // resolved (defense-in-depth on top of the libarchive SECURE flags).
        // `rel` is the path *relative to the destination* (files/ prefix already
        // stripped). Absolute paths, "..", and symlink escapes are refused.
        void ensureSafeEntry(const std::filesystem::path& destination,
                             const std::string& rel) {
            std::filesystem::path relPath(rel);
            if (relPath.is_absolute()) {
                throw error::MeowError(error::ErrorCode::ArchiveInvalid,
                    "unsafe archive entry (absolute path): " + rel);
            }
            for (const auto& part : relPath) {
                if (part == "..") {
                    throw error::MeowError(error::ErrorCode::ArchiveInvalid,
                        "unsafe archive entry (path traversal): " + rel);
                }
            }
            auto resolved = (destination / relPath).lexically_normal();
            auto base = destination.lexically_normal();
            auto baseStr = base.string();
            if (!baseStr.empty() && baseStr.back() != '/') baseStr += '/';
            auto resStr = resolved.string();
            if (resStr != base.string() && resStr.rfind(baseStr, 0) != 0) {
                throw error::MeowError(error::ErrorCode::ArchiveInvalid,
                    "unsafe archive entry (escapes destination): " + rel);
            }
        }

        // Reject a symlink or hardlink entry whose target escapes the
        // destination tree. libarchive's SECURE_SYMLINKS blocks writing *through*
        // a symlink, but we additionally refuse to *create* a link pointing
        // outside the extracted package (absolute or "../" escaping targets).
        // Hardlinks are an attack vector too (they can alias sensitive files
        // already present outside the package), so the same escape check
        // applies to their targets.
        void ensureSafeLink(const std::filesystem::path& destination,
                            const std::string& rel,
                            const char* targetRaw) {
            std::string target = targetRaw ? targetRaw : "";
            std::filesystem::path tp(target);
            if (tp.is_absolute()) {
                throw error::MeowError(error::ErrorCode::ArchiveInvalid,
                    "unsafe link (absolute target): " + rel + " -> " + target);
            }
            auto linkDir = (destination / std::filesystem::path(rel)).parent_path();
            auto resolved = (linkDir / tp).lexically_normal();
            auto base = destination.lexically_normal();
            auto baseStr = base.string();
            if (!baseStr.empty() && baseStr.back() != '/') baseStr += '/';
            auto resStr = resolved.string();
            if (resStr != base.string() && resStr.rfind(baseStr, 0) != 0) {
                throw error::MeowError(error::ErrorCode::ArchiveInvalid,
                    "unsafe link (target escapes destination): " + rel +
                    " -> " + target);
            }
        }

        // Refuse archive entry types that have no legitimate place in a
        // package and are classic extraction攻击 vectors: device nodes
        // (char/block) and FIFOs. Fail-closed.
        void ensureSafeFiletype(const std::string& rel, int filetype) {
            switch (filetype) {
                case AE_IFCHR:
                case AE_IFBLK:
                case AE_IFIFO:
                    throw error::MeowError(error::ErrorCode::ArchiveInvalid,
                        "unsafe archive entry (device node or FIFO): " + rel);
                default:
                    break;
            }
        }

        // Reject permission bits that escalate privilege on extraction:
        // setuid / setgid. A package must not install a setuid binary via the
        // archive path. Fail-closed.
        void ensureSafePerm(const std::string& rel, int perm) {
            if (perm & 06000) {  // S_ISUID | S_ISGID
                throw error::MeowError(error::ErrorCode::ArchiveInvalid,
                    "unsafe archive entry (setuid/setgid bits): " + rel);
            }
        }
    }

    Archive openArchive(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            throw error::MeowError(error::ErrorCode::FileNotFound, "archive not found: " + path.string());
        }

        ArchiveHandle _{path};
        return Archive{path};
    }

    types::FileList listFiles(const Archive& archive) {
        ArchiveHandle ah{archive.path};

        types::FileList files;
        struct archive_entry* entry;
        while (archive_read_next_header(ah.ptr, &entry) == ARCHIVE_OK) {
            auto name = norm(archive_entry_pathname(entry));
            if (!name.empty()) {
                files.value.emplace_back(name);
            }
            skipData(ah.ptr);
        }

        return files;
    }

    std::string readFile(const Archive& archive, const std::filesystem::path& filename) {
        ArchiveHandle ah{archive.path};
        auto target = filename.lexically_normal().string();
        if (target.starts_with("./")) target = target.substr(2);

        struct archive_entry* entry;
        while (archive_read_next_header(ah.ptr, &entry) == ARCHIVE_OK) {
            if (norm(archive_entry_pathname(entry)) == target) {
                std::string content;
                std::array<char, blockSize> buf{};
                la_ssize_t n;
                while ((n = archive_read_data(ah.ptr, buf.data(), buf.size())) > 0) {
                    content.append(buf.data(), static_cast<std::size_t>(n));
                }
                if (n < 0) {
                    throw error::MeowError(error::ErrorCode::ArchiveInvalid, "error reading " + filename.string());
                }
                return content;
            }
            skipData(ah.ptr);
        }

        throw error::MeowError(error::ErrorCode::FileNotFound, "file not found in archive: " + filename.string());
    }

    types::FileList listPackageContent(const Archive& archive) {
        ArchiveHandle ah{archive.path};

        types::FileList files;
        struct archive_entry* entry;
        while (archive_read_next_header(ah.ptr, &entry) == ARCHIVE_OK) {
            auto spath = norm(archive_entry_pathname(entry));

            if (spath.starts_with(filesPrefix)) {
                auto rel = spath.substr(filesPrefix.size());
                if (!rel.empty()) {
                    files.value.emplace_back(rel);
                }
            }
            skipData(ah.ptr);
        }

        return files;
    }

    types::FileList extractPackageContent(const Archive& archive, const std::filesystem::path& destination) {
        ArchiveHandle ah{archive.path};

        types::FileList extracted;
        struct archive_entry* entry;
        while (archive_read_next_header(ah.ptr, &entry) == ARCHIVE_OK) {
            auto spath = norm(archive_entry_pathname(entry));

            if (!spath.starts_with(filesPrefix)) {
                skipData(ah.ptr);
                continue;
            }

            auto rel = spath.substr(filesPrefix.size());
            if (rel.empty()) {
                skipData(ah.ptr);
                continue;
            }

            ensureSafeEntry(destination, rel);
            ensureSafeFiletype(rel, archive_entry_filetype(entry));
            ensureSafePerm(rel, archive_entry_perm(entry));
            if (archive_entry_filetype(entry) == AE_IFLNK) {
                ensureSafeLink(destination, rel, archive_entry_symlink(entry));
            } else if (archive_entry_hardlink(entry) != nullptr) {
                ensureSafeLink(destination, rel, archive_entry_hardlink(entry));
            }

            auto fullPath = destination / rel;
            archive_entry_set_pathname(entry, fullPath.c_str());

            int r = archive_read_extract(ah.ptr, entry, secureExtractFlags);
            if (r != ARCHIVE_OK) {
                throw error::MeowError(error::ErrorCode::ArchiveInvalid, "error extracting " + spath + ": " + archive_error_string(ah.ptr));
            }
            extracted.value.push_back(fullPath);
        }
        return extracted;
    }

    void extractPackageFile(const Archive& archive, const std::filesystem::path& file, const std::filesystem::path& destination) {
        ArchiveHandle ah{archive.path};

        auto target = filesPrefix + file.lexically_normal().string();
        if (target.starts_with("./")) target = target.substr(2);

        struct archive_entry* entry;
        while (archive_read_next_header(ah.ptr, &entry) == ARCHIVE_OK) {
            auto name = norm(archive_entry_pathname(entry));

            if (name == target) {
                auto rel = file.string();
                ensureSafeEntry(destination, rel);
                ensureSafeFiletype(rel, archive_entry_filetype(entry));
                ensureSafePerm(rel, archive_entry_perm(entry));
                if (archive_entry_filetype(entry) == AE_IFLNK) {
                    ensureSafeLink(destination, rel, archive_entry_symlink(entry));
            } else if (archive_entry_hardlink(entry) != nullptr) {
                    ensureSafeLink(destination, rel, archive_entry_hardlink(entry));
                }

                auto fullPath = destination / file;
                archive_entry_set_pathname(entry, fullPath.c_str());

                int r = archive_read_extract(ah.ptr, entry, secureExtractFlags);
                if (r != ARCHIVE_OK) {
                    throw error::MeowError(error::ErrorCode::ArchiveInvalid, "error extracting " + file.string() + ": " + archive_error_string(ah.ptr));
                }
                return;
            }

            skipData(ah.ptr);
        }

        throw error::MeowError(error::ErrorCode::FileNotFound, "file not found in archive: " + file.string());
    }

    std::string readPackageScript(const Archive& archive, const std::string& scriptName) {
        ArchiveHandle ah{archive.path};

        auto target = "scripts/" + scriptName;
        struct archive_entry* entry;
        while (archive_read_next_header(ah.ptr, &entry) == ARCHIVE_OK) {
            auto name = norm(archive_entry_pathname(entry));

            if (name == target) {
                std::string content;
                std::array<char, blockSize> buf{};
                la_ssize_t n;
                while ((n = archive_read_data(ah.ptr, buf.data(), buf.size())) > 0) {
                    content.append(buf.data(), static_cast<std::size_t>(n));
                }
                if (n < 0) {
                    throw error::MeowError(error::ErrorCode::ArchiveInvalid, "error reading script " + scriptName);
                }
                return content;
            }
            skipData(ah.ptr);
        }

        throw error::MeowError(error::ErrorCode::FileNotFound, "script not found in archive: " + scriptName);
    }
}
