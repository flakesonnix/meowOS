#include <meow/download/downloader.hpp>
#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace meow::download {
    std::filesystem::path downloadFile(const std::string& url, const std::filesystem::path& destination) {
        if (url.starts_with("file://")) {
            auto src = std::filesystem::path(url.substr(7));
            std::filesystem::copy_file(src, destination, std::filesystem::copy_options::overwrite_existing);
            return destination;
        }

        if (url.starts_with("http://") || url.starts_with("https://")) {
            std::string cmd = "curl -fsSL \"" + url + "\" -o \"" + destination.string() + "\"";
            int rc = std::system(cmd.c_str());
            if (rc != 0) {
                throw std::runtime_error("download failed: " + url);
            }
            return destination;
        }

        throw std::runtime_error("unsupported URL scheme: " + url);
    }

    bool verifyChecksum(const std::filesystem::path& file, const std::string& sha256) {
        std::string cmd = "sha256sum \"" + file.string() + "\"";
        std::array<char, 128> buf{};
        std::string result;

        auto* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return false;

        while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
            result += buf.data();
        }
        pclose(pipe);

        auto pos = result.find(' ');
        if (pos == std::string::npos) return false;

        return result.substr(0, pos) == sha256;
    }
}
