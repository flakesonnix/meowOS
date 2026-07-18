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
            const char* name = archive_entry_pathname(entry);
            if (name) {
                files.value.emplace_back(name);
            }
            skipData(ah.ptr);
        }

        return files;
    }

    std::string readFile(const Archive& archive, const std::filesystem::path& filename) {
        ArchiveHandle ah{archive.path};

        struct archive_entry* entry;
        while (archive_read_next_header(ah.ptr, &entry) == ARCHIVE_OK) {
            const char* name = archive_entry_pathname(entry);
            if (name && filename == std::filesystem::path(name)) {
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
            const char* name = archive_entry_pathname(entry);
            if (!name) continue;

            auto fullPath = destination / name;
            archive_entry_set_pathname(entry, fullPath.c_str());

            int r = archive_read_extract(ah.ptr, entry, ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_TIME);
            if (r != ARCHIVE_OK) {
                throw error::MeowError(error::ErrorCode::ArchiveInvalid, "error extracting " + std::string(name) + ": " + archive_error_string(ah.ptr));
            }
            extracted.value.push_back(fullPath);
        }
        return extracted;
    }

    void extractFile(const Archive& archive, const std::filesystem::path& file, const std::filesystem::path& destination) {
        ArchiveHandle ah{archive.path};

        struct archive_entry* entry;
        while (archive_read_next_header(ah.ptr, &entry) == ARCHIVE_OK) {
            const char* name = archive_entry_pathname(entry);
            if (!name) continue;

            if (file == std::filesystem::path(name)) {
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
}
