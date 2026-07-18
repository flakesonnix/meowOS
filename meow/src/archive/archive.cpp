#include <meow/archive/archive.hpp>
#include <archive.h>
#include <archive_entry.h>
#include <array>
#include <stdexcept>

namespace meow::archive {
    namespace {
        constexpr std::size_t blockSize = 10240;

        struct ArchiveHandle {
            struct archive* ptr;

            explicit ArchiveHandle(const std::filesystem::path& path) {
                ptr = archive_read_new();
                if (!ptr) {
                    throw std::runtime_error("archive_read_new failed");
                }
                archive_read_support_format_all(ptr);
                archive_read_support_filter_all(ptr);

                if (archive_read_open_filename(ptr, path.c_str(), blockSize) != ARCHIVE_OK) {
                    std::string err = archive_error_string(ptr);
                    archive_read_free(ptr);
                    ptr = nullptr;
                    throw std::runtime_error("cannot open archive: " + err);
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
                throw std::runtime_error("error skipping archive data");
            }
        }
    }

    Archive openArchive(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            throw std::runtime_error("archive not found: " + path.string());
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
                    throw std::runtime_error("error reading " + filename.string());
                }
                return content;
            }
            skipData(ah.ptr);
        }

        throw std::runtime_error("file not found in archive: " + filename.string());
    }
}
