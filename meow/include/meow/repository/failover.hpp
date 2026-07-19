#pragma once

#include <meow/config/config.hpp>
#include <meow/error/error.hpp>
#include <meow/repository/repository.hpp>
#include <meow/repository/status.hpp>

#include <functional>
#include <string>
#include <vector>

namespace meow::repository {

// One attempted mirror load for a configured source. Recorded regardless of
// outcome so the health table can show why a mirror was (or was not) used.
struct MirrorAttempt {
    std::string url;
    RepositoryStatus status = RepositoryStatus::Unavailable;
    error::ErrorCode error = error::ErrorCode::Internal;
};

// Result of loading one configured source across its mirrors. On success,
// `repository` holds the data from the selected mirror. `attempts` always lists
// every mirror that was tried, in order, so a failed source still exposes its
// full attempt history (never silently dropped).
struct SourceLoadResult {
    bool success = false;
    Repository repository;
    RepositoryStatus status = RepositoryStatus::Unavailable;
    std::vector<MirrorAttempt> attempts;
};

// True iff the error justifies trying the next mirror. Only *transport*
// failures are failover-eligible:
//   timeout, DNS failure, connection refused/reset, HTTP 5xx
// Trust failures are NOT: bad signature, expired metadata, invalid
// repository_id / metadata, checksum mismatch, HTTP 4xx (incl. 404), and
// missing package. Those mean the data is suspect, so we stop rather than
// land on another copy of the same untrusted data.
bool isFailoverAllowed(const error::MeowError& e);

// Try each mirror in listed order. The first that loads and verifies wins.
// On a trust failure the chain aborts immediately (no further mirrors tried).
// `load` is injected so the backend layer stays the single owner of transport;
// it should call the appropriate IRepositoryBackend for the given URL.
SourceLoadResult loadRepositoryWithFailover(
    const std::vector<std::string>& mirrors,
    const std::function<Repository(const std::string& url)>& load);

}  // namespace meow::repository
