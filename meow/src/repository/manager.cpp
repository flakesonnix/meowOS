#include "meow/repository/manager.hpp"

#include <meow/error/error.hpp>
#include <meow/log/logger.hpp>
#include <meow/repository/version.hpp>

#include <algorithm>
#include <map>
#include <optional>

namespace meow::repository {

namespace {

// Latest version string for a package, or nullopt if it has none.
std::optional<types::PackageVersion> latestOf(const RepositoryPackage& pkg) {
    const auto* v = latestVersion(pkg);
    if (!v) return std::nullopt;
    return *v;
}

}  // namespace

RepositoryManager::RepositoryManager(const config::Config& cfg) : cfg_(cfg) {
    // Refresh every configured source concurrently. Each source is loaded and
    // verified independently through the existing failover policy; a broken
    // source never blocks the others. Results come back in input order so the
    // merged view (priority/version selection) is deterministic regardless of
    // which source finished first.
    auto results = refreshRepositories(cfg.repositories, cfg.downloadWorkers);
    for (auto& r : results) {
        RepositoryState state;
        state.config = r.config;
        state.status = r.status;
        state.attempts = std::move(r.attempts);
        if (r.repository) {
            state.repository = std::move(*r.repository);
        } else if (!state.attempts.empty()) {
            const auto& last = state.attempts.back();
            state.error = error::MeowError(
                last.error, "all mirrors failed for '" + r.config.id + "'");
            if (lastError_.empty() && state.error)
                lastError_ = error::formatError(*state.error);
        }
        if (state.status != RepositoryStatus::Available) ++failed_;
        loaded_.push_back(std::move(state));
    }
    merged_ = buildMerged();
}

std::vector<RepositoryManager::Failed> RepositoryManager::failures() const {
    std::vector<Failed> out;
    for (const auto& s : loaded_) {
        if (s.status == RepositoryStatus::Available) continue;
        std::string msg = s.error ? error::formatError(*s.error) : "load failed";
        std::string endpoint = s.config.mirrors.empty()
                                   ? s.config.url
                                   : s.config.mirrors.front();
        out.push_back(Failed{s.config.id, endpoint, msg});
    }
    return out;
}

Repository RepositoryManager::buildMerged() const {
    // Gather, per package name, every repository that provides it together
    // with that repository's priority and latest version.
    struct Candidate {
        const RepositoryPackage* pkg;
        int priority;
        types::PackageVersion latest;
    };
    std::map<std::string, Candidate> best;

    for (const auto& l : loaded_) {
        for (const auto& pkg : l.repository.packages) {
            auto lv = latestOf(pkg);
            if (!lv) continue;
            auto it = best.find(pkg.name.value);
            if (it == best.end()) {
                best[pkg.name.value] = Candidate{&pkg, l.config.priority, *lv};
            } else {
                // Higher priority wins. On a tie, higher latest version wins.
                bool better = l.config.priority > it->second.priority ||
                              (l.config.priority == it->second.priority &&
                               compareVersions(*lv, it->second.latest) > 0);
                if (better) {
                    it->second = Candidate{&pkg, l.config.priority, *lv};
                }
            }
        }
    }

    Repository merged;
    if (!loaded_.empty()) {
        merged.name = loaded_.front().repository.name;
        merged.id = loaded_.front().repository.id;
        merged.cache = loaded_.front().repository.cache;
        merged.generated = loaded_.front().repository.generated;
        merged.expires = loaded_.front().repository.expires;
        merged.mirrors = loaded_.front().repository.mirrors;
    }

    for (const auto& [name, c] : best) {
        merged.packages.push_back(*c.pkg);
    }
    return merged;
}

const RepositoryPackage* RepositoryManager::findPackage(
    const types::PackageName& name) const {
    return repository::findPackage(merged_, name);
}

}  // namespace meow::repository
