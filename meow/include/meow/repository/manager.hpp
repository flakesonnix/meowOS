#pragma once

#include <meow/config/config.hpp>
#include <meow/repository/backend.hpp>
#include <meow/repository/repository.hpp>

#include <string>
#include <vector>

namespace meow::repository {

// Owns the set of configured repositories and presents them to the resolver
// and commands as a single, unified package space. The resolver/installer
// never learn how many repositories exist or which transport backs them; they
// only see the merged view.
//
// Loading is tolerant: a misconfigured or unreachable repository is recorded
// as a failure and skipped, so one broken source never takes down the others.
class RepositoryManager {
public:
    struct Loaded {
        config::RepositoryConfig config;
        Repository repository;
    };

    explicit RepositoryManager(const config::Config& cfg);

    // Successfully loaded repositories (in config order).
    const std::vector<Loaded>& repositories() const { return loaded_; }

    // Number of configured repositories that failed to load.
    std::size_t failedCount() const { return failed_; }

    // The message from the first repository load failure (empty if all
    // loaded). Useful for surfacing why no repository is available.
    const std::string& lastError() const { return lastError_; }

    // A merged, in-memory Repository representing the union of all loaded
    // repositories. For each package name, the chosen version set comes from
    // the highest-priority repository that contains it; ties in priority are
    // broken by the highest latest version. This implements the rule
    // "repository priority wins, then version".
    const Repository& mergedRepository() const { return merged_; }

    // Find a package by name in the merged view.
    const RepositoryPackage* findPackage(const types::PackageName& name) const;

private:
    std::vector<Loaded> loaded_;
    std::size_t failed_ = 0;
    config::Config cfg_;
    Repository merged_;
    std::string lastError_;

    Repository buildMerged() const;
};

}  // namespace meow::repository
