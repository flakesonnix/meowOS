#include <meow/download/downloader.hpp>
#include <meow/error/error.hpp>
#include <array>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <fstream>
#include <curl/curl.h>

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

        struct WriteCtx {
            std::ofstream* out;
            uint64_t bytes = 0;
            uint64_t maxBytes = 0;
            bool exceeded = false;
        };

        size_t writeCb(void* ptr, size_t size, size_t nmemb, void* userdata) {
            auto* ctx = static_cast<WriteCtx*>(userdata);
            size_t total = size * nmemb;
            if (ctx->maxBytes > 0 && ctx->bytes + total > ctx->maxBytes) {
                ctx->exceeded = true;
                return 0; // aborts transfer -> CURL_WRITE_ERROR
            }
            ctx->out->write(static_cast<const char*>(ptr), static_cast<std::streamsize>(total));
            if (!ctx->out->good()) {
                ctx->exceeded = true;
                return 0;
            }
            ctx->bytes += total;
            return total;
        }

        struct HeaderCtx {
            std::string etag;
            std::string lastModified;
            std::optional<uint64_t> contentLength;
        };

        size_t headerCb(char* buffer, size_t size, size_t nmemb, void* userdata) {
            auto* ctx = static_cast<HeaderCtx*>(userdata);
            size_t total = size * nmemb;
            std::string line(buffer, total);

            auto lower = line;
            for (auto& c : lower) c = static_cast<char>(std::tolower(c));

            if (lower.starts_with("etag:")) {
                ctx->etag = line.substr(5);
                while (!ctx->etag.empty() && (ctx->etag.front() == ' ' || ctx->etag.front() == '\t' || ctx->etag.front() == '"' || ctx->etag.front() == '\r' || ctx->etag.front() == '\n'))
                    ctx->etag.erase(ctx->etag.begin());
                while (!ctx->etag.empty() && (ctx->etag.back() == '"' || ctx->etag.back() == '\r' || ctx->etag.back() == '\n'))
                    ctx->etag.pop_back();
            } else if (lower.starts_with("last-modified:")) {
                ctx->lastModified = line.substr(14);
                while (!ctx->lastModified.empty() && (ctx->lastModified.front() == ' ' || ctx->lastModified.front() == '\t' || ctx->lastModified.front() == '\r' || ctx->lastModified.front() == '\n'))
                    ctx->lastModified.erase(ctx->lastModified.begin());
                while (!ctx->lastModified.empty() && (ctx->lastModified.back() == '\r' || ctx->lastModified.back() == '\n'))
                    ctx->lastModified.pop_back();
            } else if (lower.starts_with("content-length:")) {
                try {
                    ctx->contentLength = std::stoull(line.substr(15));
                } catch (...) {}
            }
            return total;
        }

        bool isRetryableHttp(long code) {
            // 5xx are transient; everything else (incl. 4xx) is terminal.
            return code >= 500 && code < 600;
        }

        bool isRetryableCurl(CURLcode rc) {
            switch (rc) {
                case CURLE_COULDNT_CONNECT:
                case CURLE_COULDNT_RESOLVE_HOST:
                case CURLE_OPERATION_TIMEDOUT:
                case CURLE_GOT_NOTHING:
                case CURLE_RECV_ERROR:
                case CURLE_SEND_ERROR:
                case CURLE_PARTIAL_FILE:
                case CURLE_SSL_CONNECT_ERROR:
                    return true;
                default:
                    return false;
            }
        }

        DownloadResult performHttp(
            const std::string& url,
            const std::filesystem::path& destination,
            const DownloadOptions& options
        ) {
            auto part = destination;
            part += ".part";

            WriteCtx wctx{nullptr, 0, options.maxBytes, false};
            HeaderCtx hctx;

            int attempts = options.retries < 0 ? 0 : options.retries;
            CURLcode lastRc = CURLE_OK;
            long lastHttp = 0;

            for (int attempt = 0; attempt <= attempts; ++attempt) {
                // Fresh truncating handle each attempt so a failed/partial
                // response (e.g. a 5xx error body) never leaks into the next.
                std::ofstream out(part, std::ios::binary | std::ios::trunc);
                if (!out) {
                    abortDownload(part);
                    throw error::MeowError(error::ErrorCode::DownloadFailed,
                        "cannot open download target: " + part.string());
                }
                wctx.out = &out;
                wctx.bytes = 0;
                wctx.exceeded = false;
                hctx = HeaderCtx{};

                CURL* curl = curl_easy_init();
                if (!curl) {
                    abortDownload(part);
                    throw error::MeowError(error::ErrorCode::DownloadFailed, "cannot init curl");
                }

                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &wctx);
                curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCb);
                curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hctx);
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(options.timeoutSeconds));
                curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(options.timeoutSeconds));
                curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L); // we inspect HTTP code ourselves
                if (!options.verifyTls) {
                    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
                }
                if (options.etag) {
                    std::string hdr = "If-None-Match: " + *options.etag;
                    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,
                        curl_slist_append(nullptr, hdr.c_str()));
                }

                // Pre-flight size guard from Content-Length (available after header parse).
                CURLcode rc = curl_easy_perform(curl);
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &lastHttp);

                if (rc == CURLE_OK && lastHttp == 304) {
                    curl_easy_cleanup(curl);
                    abortDownload(part); // keep existing destination untouched
                    DownloadResult r;
                    r.path = destination;
                    r.notModified = true;
                    r.etag = hctx.etag;
                    r.lastModified = hctx.lastModified;
                    return r;
                }

                bool abortSize = wctx.exceeded;

                if (rc == CURLE_OK && !abortSize && lastHttp >= 200 && lastHttp < 300) {
                    // Size guard from declared Content-Length before finalizing.
                    if (options.maxBytes > 0 && hctx.contentLength && *hctx.contentLength > options.maxBytes) {
                        curl_easy_cleanup(curl);
                        abortDownload(part);
                        throw error::MeowError(error::ErrorCode::DownloadInterrupted,
                            "download exceeds max size (" + std::to_string(*hctx.contentLength) +
                            " > " + std::to_string(options.maxBytes) + "): " + url);
                    }
                    out.flush();
                    curl_easy_cleanup(curl);
                    DownloadResult r;
                    r.path = finalizeDownload(part, destination);
                    r.size = wctx.bytes;
                    r.etag = hctx.etag;
                    r.lastModified = hctx.lastModified;
                    r.notModified = false;
                    return r;
                }

                lastRc = rc;
                curl_easy_cleanup(curl);

                // Terminal (non-retryable) conditions.
                if (rc == CURLE_OK) {
                    // HTTP-level error
                    if (lastHttp == 404) {
                        abortDownload(part);
                        throw error::MeowError(error::ErrorCode::DownloadHttpError,
                            "download not found (404): " + url);
                    }
                    if (lastHttp >= 400 && lastHttp < 500) {
                        abortDownload(part);
                        throw error::MeowError(error::ErrorCode::DownloadHttpError,
                            "download rejected (HTTP " + std::to_string(lastHttp) + "): " + url);
                    }
                    if (!isRetryableHttp(lastHttp)) {
                        abortDownload(part);
                        throw error::MeowError(error::ErrorCode::DownloadHttp5xx,
                            "download failed (HTTP " + std::to_string(lastHttp) + "): " + url);
                    }
                } else if (rc == CURLE_WRITE_ERROR && abortSize) {
                    abortDownload(part);
                    throw error::MeowError(error::ErrorCode::DownloadInterrupted,
                        "download exceeds max size: " + url);
                } else if (rc == CURLE_OPERATION_TIMEDOUT) {
                    abortDownload(part);
                    throw error::MeowError(error::ErrorCode::DownloadTimeout,
                        "download timed out: " + url);
                } else if (!isRetryableCurl(rc)) {
                    abortDownload(part);
                    throw error::MeowError(error::ErrorCode::DownloadFailed,
                        "download failed (curl " + std::string(curl_easy_strerror(rc)) + "): " + url);
                }

                // Retryable: loop continues.
            }

            abortDownload(part);
            if (lastRc == CURLE_OK) {
                throw error::MeowError(error::ErrorCode::DownloadHttpError,
                    "download failed after retries (HTTP " + std::to_string(lastHttp) + "): " + url);
            }
            throw error::MeowError(error::ErrorCode::DownloadFailed,
                "download failed after retries (curl " + std::string(curl_easy_strerror(lastRc)) + "): " + url);
        }

        DownloadResult performFile(
            const std::string& url,
            const std::filesystem::path& destination,
            const DownloadOptions& options
        ) {
            auto src = std::filesystem::path(url.substr(7));
            if (!std::filesystem::exists(src)) {
                throw error::MeowError(error::ErrorCode::InvalidDownload, "source not found: " + src.string());
            }

            uintmax_t sz = std::filesystem::file_size(src);
            if (options.maxBytes > 0 && sz > static_cast<uintmax_t>(options.maxBytes)) {
                throw error::MeowError(error::ErrorCode::DownloadInterrupted,
                    "source exceeds max size: " + src.string());
            }

            auto part = destination;
            part += ".part";
            std::filesystem::copy_file(src, part, std::filesystem::copy_options::overwrite_existing);
            DownloadResult r;
            r.path = finalizeDownload(part, destination);
            r.size = static_cast<uint64_t>(sz);
            return r;
        }
    }

    DownloadResult downloadFile(
        const std::string& url,
        const std::filesystem::path& destination,
        const DownloadOptions& options
    ) {
        if (url.starts_with("file://")) {
            return performFile(url, destination, options);
        }
        if (url.starts_with("http://") || url.starts_with("https://")) {
            return performHttp(url, destination, options);
        }
        throw error::MeowError(error::ErrorCode::InvalidDownload, "unsupported URL scheme: " + url);
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
