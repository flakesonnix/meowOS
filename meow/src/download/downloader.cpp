#include <meow/download/downloader.hpp>
#include <meow/error/error.hpp>
#include <array>
#include <cstdio>

namespace meow::download {
    namespace {
        std::filesystem::path finalizeDownload(
            const std::filesystem::path& part,
            const std::filesystem::path& destination
        ) {
            std::error_code ec;
            std::filesystem::rename(part, destination, ec);
            if (ec) {
                std::filesystem::remove(part, ec);
                throw error::MeowError(
                    error::ErrorCode::DownloadFailed,
                    "cannot finalize download: " + destination.string());
            }
            return destination;
        }

        void abortDownload(const std::filesystem::path& part) {
            std::error_code ec;
            std::filesystem::remove(part, ec);
        }
    }

    std::filesystem::path downloadFile(
        const std::string& url,
        const std::filesystem::path& destination,
        const DownloadOptions& options
    ) {
        auto part = destination;
        part += ".part";

        if (url.starts_with("file://")) {
            auto src = std::filesystem::path(url.substr(7));
            if (!std::filesystem::exists(src)) {
                throw error::MeowError(error::ErrorCode::DownloadFailed, "source not found: " + src.string());
            }
            std::filesystem::copy_file(src, part, std::filesystem::copy_options::overwrite_existing);
            return finalizeDownload(part, destination);
        }

        if (url.starts_with("http://") || url.starts_with("https://")) {
            long secs = options.timeout.count() > 0 ? options.timeout.count() : 30;
            std::string cmd = "curl -fsSL --fail --retry 3 --retry-delay 1 -C -";
            cmd += " --connect-timeout " + std::to_string(secs);
            cmd += " --max-time " + std::to_string(secs);
            if (!options.verifyTls) {
                cmd += " --insecure";
            }
            if (options.etag) {
                cmd += " -H \"If-None-Match: " + *options.etag + "\"";
            }
            cmd += " \"" + url + "\" -o \"" + part.string() + "\"";

            int rc = std::system(cmd.c_str());
            if (rc != 0) {
                abortDownload(part);
                throw error::MeowError(
                    error::ErrorCode::DownloadFailed,
                    "download failed (curl rc=" + std::to_string(rc) + "): " + url);
            }
            return finalizeDownload(part, destination);
        }

        throw error::MeowError(error::ErrorCode::DownloadFailed, "unsupported URL scheme: " + url);
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

    std::string computeFileHash(const std::filesystem::path& file) {
        std::string cmd = "sha256sum \"" + file.string() + "\"";
        std::array<char, 128> buf{};
        std::string result;

        auto* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            throw error::MeowError(error::ErrorCode::Internal, "cannot compute hash for " + file.string());
        }

        while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
            result += buf.data();
        }
        pclose(pipe);

        auto pos = result.find(' ');
        if (pos == std::string::npos) {
            throw error::MeowError(error::ErrorCode::Internal, "cannot parse hash for " + file.string());
        }

        return result.substr(0, pos);
    }
}
