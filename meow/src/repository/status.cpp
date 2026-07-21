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
        case error::ErrorCode::DownloadHttp5xx:
        case error::ErrorCode::DownloadFailed:
        case error::ErrorCode::RepositoryNotFound:
            return RepositoryStatus::NetworkError;

        case error::ErrorCode::RepositoryExpired:
            return RepositoryStatus::Expired;

        case error::ErrorCode::InvalidSignature:
        case error::ErrorCode::TrustedKeyNotFound:
        // v0.7 signed package index: a missing (strict) or invalid index is a
        // signature/trust failure, mirroring repository.toml signature policy.
        case error::ErrorCode::MissingPackageIndex:
        case error::ErrorCode::InvalidPackageIndex:
            return RepositoryStatus::InvalidSignature;

        case error::ErrorCode::InvalidRepository:
        case error::ErrorCode::InvalidManifest:
        // A manifest whose hash disagrees with the signed index is untrusted
        // metadata (the per-package manifest is no longer authenticated alone).
        case error::ErrorCode::PackageIndexMismatch:
            return RepositoryStatus::InvalidMetadata;

        default:
            // Anything else is treated as a non-specific load failure.
            return RepositoryStatus::Unavailable;
    }
}

}  // namespace meow::repository
