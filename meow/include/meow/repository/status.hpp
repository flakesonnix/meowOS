#pragma once

#include <meow/error/error.hpp>

#include <string>

namespace meow::repository {

// Runtime health of a single configured repository source. This is *runtime*
// state (did the last load/refresh succeed, and why not), deliberately kept
// separate from `Repository`, which is trusted, parsed metadata. A repository
// may be present in the manager with a non-Available status; it is never
// silently dropped.
enum class RepositoryStatus {
    Available,        // loaded and verified successfully
    Unavailable,      // transient: source could not be reached
    NetworkError,     // connection refused / timeout / DNS / curl transport error
    Expired,          // repository.toml expiry is in the past
    InvalidSignature, // signature missing or does not verify against a trusted key
    InvalidMetadata   // repository.toml present but malformed / wrong format version
};

// Human-readable label for a status, used by the CLI health table.
inline const char* statusLabel(RepositoryStatus s) {
    switch (s) {
        case RepositoryStatus::Available:        return "Available";
        case RepositoryStatus::Unavailable:      return "Unavailable";
        case RepositoryStatus::NetworkError:     return "NetworkError";
        case RepositoryStatus::Expired:          return "Expired";
        case RepositoryStatus::InvalidSignature: return "InvalidSignature";
        case RepositoryStatus::InvalidMetadata:  return "InvalidMetadata";
    }
    return "Unknown";
}

// Central mapping from a repository load failure to a runtime status. This is
// the single place that classifies errors; callers (e.g. failover logic)
// consume the status rather than re-matching error codes.
RepositoryStatus classifyRepositoryError(const error::MeowError& e);

}  // namespace meow::repository
