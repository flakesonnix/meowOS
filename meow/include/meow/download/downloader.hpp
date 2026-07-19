#ifndef MEOWOS_DOWNLOADER_H
#define MEOWOS_DOWNLOADER_H

#include <filesystem>
#include <string>
#include <optional>
#include <chrono>

namespace meow::download {

struct DownloadOptions {
    int retries = 3;
    int timeoutSeconds = 30;
    bool verifyTls = true;
    std::optional<std::string> etag;
    uint64_t maxBytes = 0; // 0 = unlimited; enforces Content-Length / file size cap
};

struct DownloadResult {
    std::filesystem::path path;
    uint64_t size = 0;
    std::string etag;
    std::string lastModified;
    bool notModified = false; // 304 Not Modified: existing file reused
};

// Downloads to <destination>.part and atomically renames into place.
// On failure the partial file is removed and the existing cache (if any)
// is left untouched. For file:// the source is copied atomically.
DownloadResult downloadFile(
    const std::string& url,
    const std::filesystem::path& destination,
    const DownloadOptions& options = {}
);

bool verifyChecksum(const std::filesystem::path& file, const std::string& sha256);
std::string computeFileHash(const std::filesystem::path& file);

}

#endif
