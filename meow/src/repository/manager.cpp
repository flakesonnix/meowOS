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
    for (const auto& rc : cfg.repositories) {
        RepositoryState state;
        state.config = rc;

        // Mirrors are alternate transport locations for the *same* repository
        // identity. The failover policy decides when to move to the next mirror:
        // transport failures (timeout, DNS/connection refused, HTTP 5xx) are
        // retried; trust failures (bad signature, expired, invalid metadata/id)
        // abort the chain immediately so a bad mirror is never papered over by
        // another copy of the same untrusted data. The backend layer stays the
        // single owner of transport.
        auto result = loadRepositoryWithFailover(
            rc.urls(), [](const std::string& url) {
                return createBackend(url)->loadRepository();
            });

        state.attempts = std::move(result.attempts);
        state.status = result.status;
        if (result.success) {
            state.repository = std::move(result.repository);
        } else if (!state.attempts.empty()) {
            const auto& last = state.attempts.back();
            state.error =
                error::MeowError(last.error, "all mirrors failed for '" + rc.id + "'");
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
