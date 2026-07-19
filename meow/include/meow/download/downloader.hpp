#ifndef MEOWOS_DOWNLOADER_H
#define MEOWOS_DOWNLOADER_H

#include <filesystem>
#include <string>
#include <optional>
#include <chrono>

namespace meow::download {

struct DownloadOptions {
    std::chrono::seconds timeout{30};
    bool verifyTls{true};
    std::optional<std::string> etag;
};

std::filesystem::path downloadFile(
    const std::string& url,
    const std::filesystem::path& destination,
    const DownloadOptions& options = {}
);

bool verifyChecksum(const std::filesystem::path& file, const std::string& sha256);
std::string computeFileHash(const std::filesystem::path& file);

}

#endif
