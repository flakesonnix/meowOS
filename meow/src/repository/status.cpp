#include "meow/repository/status.hpp"

#include <meow/error/error.hpp>

namespace meow::repository {

// Map a repository load failure to a runtime status. This is the single place
// that decides how a thrown error is classified; future failover rules consume
// the resulting status rather than re-matching error codes. Trust-related
// failures (signature, expiry, malformed metadata) are reported distinctly from
// transport failures so availability logic never weakens trust.
RepositoryStatus classifyRepositoryError(const error::MeowError& e) {
    switch (e.code) {
        case error::ErrorCode::DownloadTimeout:
        case error::ErrorCode::DownloadInterrupted:
        case error::ErrorCode::DownloadHttpError:
        case error::ErrorCode::DownloadHttp5xx:
        case error::ErrorCode::DownloadFailed:
        case error::ErrorCode::RepositoryNotFound:
            return RepositoryStatus::NetworkError;

        case error::ErrorCode::RepositoryExpired:
            return RepositoryStatus::Expired;

        case error::ErrorCode::InvalidSignature:
        case error::ErrorCode::TrustedKeyNotFound:
            return RepositoryStatus::InvalidSignature;

        case error::ErrorCode::InvalidRepository:
        case error::ErrorCode::InvalidManifest:
            return RepositoryStatus::InvalidMetadata;

        default:
            // Anything else is treated as a non-specific load failure.
            return RepositoryStatus::Unavailable;
    }
}

}  // namespace meow::repository
