#include "meow/repository/failover.hpp"

#include <meow/log/logger.hpp>

namespace meow::repository {

bool isFailoverAllowed(const error::MeowError& e) {
    switch (e.code) {
        // Transport-level problems: another mirror may well serve the same,
        // trustworthy metadata, so retrying is safe.
        case error::ErrorCode::DownloadTimeout:
        case error::ErrorCode::DownloadInterrupted:
        case error::ErrorCode::DownloadFailed:
        case error::ErrorCode::DownloadHttp5xx:
            return true;

        // Everything else is a trust or hard failure: bad signature, expired
        // metadata, malformed/invalid repository, missing package, checksum
        // mismatch, or HTTP 4xx (incl. 404). Falling over would only reach
        // another copy of the same untrusted data, so it is forbidden.
        default:
            return false;
    }
}

SourceLoadResult loadRepositoryWithFailover(
    const std::vector<std::string>& mirrors,
    const std::function<Repository(const std::string& url)>& load) {
    SourceLoadResult result;

    if (mirrors.empty()) {
        result.status = RepositoryStatus::Unavailable;
        result.attempts.push_back(
            MirrorAttempt{"", RepositoryStatus::Unavailable,
                          error::ErrorCode::RepositoryNotFound});
        return result;
    }

    for (const auto& url : mirrors) {
        MirrorAttempt attempt;
        attempt.url = url;
        try {
            result.repository = load(url);
            attempt.status = RepositoryStatus::Available;
            attempt.error = error::ErrorCode::Internal;
            result.attempts.push_back(std::move(attempt));
            result.success = true;
            result.status = RepositoryStatus::Available;
            return result;
        } catch (const error::MeowError& e) {
            attempt.status = classifyRepositoryError(e);
            attempt.error = e.code;
            result.attempts.push_back(attempt);

            if (isFailoverAllowed(e)) {
                log::log(log::LogLevel::Warning,
                         "mirror '" + url + "' unavailable (" +
                             statusLabel(attempt.status) +
                             "), trying next mirror");
                continue;
            }
            // Trust/hard failure: stop the chain. The source is rejected.
            log::log(log::LogLevel::Warning,
                     "mirror '" + url + "' failed trust/metadata check (" +
                         statusLabel(attempt.status) + "), not failing over");
            result.success = false;
            result.status = attempt.status;
            return result;
        } catch (const std::exception& e) {
            attempt.status = RepositoryStatus::Unavailable;
            attempt.error = error::ErrorCode::Internal;
            result.attempts.push_back(attempt);
            log::log(log::LogLevel::Warning,
                     "mirror '" + url + "' unavailable (" +
                         std::string(e.what()) + "), trying next mirror");
            continue;
        }
    }

    // Every mirror failed with a transport error.
    result.success = false;
    result.status = RepositoryStatus::NetworkError;
    return result;
}

}  // namespace meow::repository
