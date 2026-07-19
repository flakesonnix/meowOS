#pragma once

#include <meow/config/config.hpp>
#include <meow/repository/failover.hpp>
#include <meow/repository/repository.hpp>
#include <meow/repository/status.hpp>

#include <optional>
#include <string>
#include <vector>

namespace meow::repository {

// Outcome of refreshing one configured source. `config` is echoed back so the
// caller can correlate results with inputs by index. `repository` is populated
// only on success; `attempts` records every mirror that was tried.
struct RefreshResult {
    config::RepositoryConfig config;
    RepositoryStatus status = RepositoryStatus::Unavailable;
    std::optional<Repository> repository;
    std::vector<MirrorAttempt> attempts;
};

// Refresh every configured source concurrently using a bounded worker pool.
// Each source is loaded and verified independently through the existing
// failover policy; a broken source is recorded as a non-Available result and
// never blocks the others. Results are returned in the SAME order as `repos`
// so downstream selection (priority/version) is deterministic regardless of
// which source finished first. `workers == 0` defaults to
// min(hardware_concurrency, 8), matching the package-download pool.
std::vector<RefreshResult> refreshRepositories(
    const std::vector<config::RepositoryConfig>& repos,
    size_t workers = 0);

}  // namespace meow::repository
