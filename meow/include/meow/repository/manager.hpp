#pragma once

#include <meow/config/config.hpp>
#include <meow/error/error.hpp>
#include <meow/repository/backend.hpp>
#include <meow/repository/failover.hpp>
#include <meow/repository/refresh.hpp>
#include <meow/repository/repository.hpp>
#include <meow/repository/status.hpp>

#include <optional>
#include <string>
#include <vector>

namespace meow::repository {

// Owns the set of configured repositories and presents them to the resolver
// and commands as a single, unified package space. The resolver/installer
// never learn how many repositories exist or which transport backs them; they
// only see the merged view.
//
// Loading is tolerant: a misconfigured or unreachable repository is recorded
// with a non-Available status and skipped for the merged view, so one broken
// source never takes down the others. A failed source still exists in the
// manager (see RepositoryState) so its status and error are visible.
class RepositoryManager {
public:
    // One configured source and its runtime state. Present for every configured
    // repository regardless of load outcome; `status` distinguishes healthy from
    // failed, and `error` carries the last failure when `status != Available`.
    struct RepositoryState {
        config::RepositoryConfig config;
        Repository repository;
        RepositoryStatus status = RepositoryStatus::Available;
        std::optional<error::MeowError> error;
        std::vector<MirrorAttempt> attempts;
    };

    explicit RepositoryManager(const config::Config& cfg);

    // Every configured repository and its runtime state (in config order).
    // Includes sources that failed to load.
    const std::vector<RepositoryState>& repositories() const { return loaded_; }

    // Number of configured repositories that are not Available.
    std::size_t failedCount() const { return failed_; }

    // Number of configured repositories that loaded successfully (status
    // Available). Zero means no usable repository is configured.
    std::size_t availableCount() const {
        std::size_t n = 0;
        for (const auto& s : loaded_)
            if (s.status == RepositoryStatus::Available) ++n;
        return n;
    }

    // The message from the first non-Available repository (empty if all
    // loaded). Useful for surfacing why no repository is available.
    const std::string& lastError() const { return lastError_; }

    // Per-repository load failures (config id, url, error message). Derived from
    // the states that are not Available.
    struct Failed {
        std::string id;
        std::string url;
        std::string error;
    };
    std::vector<Failed> failures() const;

    // A merged, in-memory Repository representing the union of all Available
    // repositories. For each package name, the chosen version set comes from
    // the highest-priority repository that contains it; ties in priority are
    // broken by the highest latest version. This implements the rule
    // "repository priority wins, then version".
    const Repository& mergedRepository() const { return merged_; }

    // Find a package by name in the merged view.
    const RepositoryPackage* findPackage(const types::PackageName& name) const;

private:
    std::vector<RepositoryState> loaded_;
    std::size_t failed_ = 0;
    config::Config cfg_;
    Repository merged_;
    std::string lastError_;

    Repository buildMerged() const;
};

}  // namespace meow::repository
