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

        std::string norm(const char* raw) {
            std::string s = raw ? raw : "";
            if (s.starts_with("./")) s = s.substr(2);
            if (s.ends_with("/") && s.size() > 1) s.pop_back();
            return s;
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

    types::FileList extractAll(const Archive& archive, const std::filesystem::path& destination) {
        ArchiveHandle ah{archive.path};

        types::FileList extracted;
        struct archive_entry* entry;
        while (archive_read_next_header(ah.ptr, &entry) == ARCHIVE_OK) {
            auto name = norm(archive_entry_pathname(entry));
            if (name.empty()) { skipData(ah.ptr); continue; }

            auto fullPath = destination / name;
            archive_entry_set_pathname(entry, fullPath.c_str());

            int r = archive_read_extract(ah.ptr, entry, ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_TIME);
            if (r != ARCHIVE_OK) {
                throw error::MeowError(error::ErrorCode::ArchiveInvalid, "error extracting " + name + ": " + archive_error_string(ah.ptr));
            }
            extracted.value.push_back(fullPath);
        }
        return extracted;
    }

    void extractFile(const Archive& archive, const std::filesystem::path& file, const std::filesystem::path& destination) {
        ArchiveHandle ah{archive.path};
        auto target = file.lexically_normal().string();
        if (target.starts_with("./")) target = target.substr(2);

        struct archive_entry* entry;
        while (archive_read_next_header(ah.ptr, &entry) == ARCHIVE_OK) {
            auto name = norm(archive_entry_pathname(entry));
            if (name.empty()) { skipData(ah.ptr); continue; }

            if (name == target) {
                auto fullPath = destination / name;
                archive_entry_set_pathname(entry, fullPath.c_str());

                int r = archive_read_extract(ah.ptr, entry, ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_TIME);
                if (r != ARCHIVE_OK) {
                    throw error::MeowError(error::ErrorCode::ArchiveInvalid, "error extracting " + file.string() + ": " + archive_error_string(ah.ptr));
                }
                return;
            }

            skipData(ah.ptr);
        }

        throw error::MeowError(error::ErrorCode::FileNotFound, "file not found in archive: " + file.string());
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

            auto fullPath = destination / rel;
            archive_entry_set_pathname(entry, fullPath.c_str());

            int r = archive_read_extract(ah.ptr, entry, ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_TIME);
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
                auto fullPath = destination / file;
                archive_entry_set_pathname(entry, fullPath.c_str());

                int r = archive_read_extract(ah.ptr, entry, ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_TIME);
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
