#ifndef MEOWOS_DOWNLOADER_H
#define MEOWOS_DOWNLOADER_H

#include <filesystem>
#include <string>

namespace meow::download {
    std::filesystem::path downloadFile(const std::string& url, const std::filesystem::path& destination);
    bool verifyChecksum(const std::filesystem::path& file, const std::string& sha256);
    std::string computeFileHash(const std::filesystem::path& file);
}

#endif //MEOWOS_DOWNLOADER_H
