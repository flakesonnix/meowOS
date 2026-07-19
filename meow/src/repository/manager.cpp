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

        const auto endpoints = rc.urls();
        // Mirrors are alternate transport locations for the *same* repository
        // identity. Try them in listed order; the first that loads and verifies
        // wins. Trust failures (bad signature, expired, invalid id) are not
        // "mirror retries" -- they are fatal for the whole source and must not
        // be papered over by trying another mirror, so we stop on those.
        bool loaded = false;
        for (const auto& endpoint : endpoints) {
            try {
                auto backend = createBackend(endpoint);
                state.repository = backend->loadRepository();
                state.status = RepositoryStatus::Available;
                state.error.reset();
                loaded = true;
                break;
            } catch (const error::MeowError& e) {
                if (e.code == error::ErrorCode::InvalidSignature ||
                    e.code == error::ErrorCode::TrustedKeyNotFound ||
                    e.code == error::ErrorCode::RepositoryExpired ||
                    e.code == error::ErrorCode::InvalidRepository ||
                    e.code == error::ErrorCode::InvalidManifest) {
                    // Trust failure: do not fall through to another mirror.
                    state.status = classifyRepositoryError(e);
                    state.error = e;
                    log::log(log::LogLevel::Warning,
                             "repository '" + rc.id + "' (" + endpoint +
                                 ") trust failure: " + e.what());
                    loaded = true;  // stop trying further mirrors
                    break;
                }
                state.status = classifyRepositoryError(e);
                state.error = e;
                if (lastError_.empty()) lastError_ = error::formatError(e);
                log::log(log::LogLevel::Warning,
                         "repository '" + rc.id + "' (" + endpoint +
                             ") unavailable, trying next mirror: " + e.what());
            } catch (const std::exception& e) {
                state.status = RepositoryStatus::Unavailable;
                state.error = error::MeowError(error::ErrorCode::Internal, e.what());
                if (lastError_.empty()) lastError_ = e.what();
                log::log(log::LogLevel::Warning,
                         "repository '" + rc.id + "' (" + endpoint +
                             ") unavailable, trying next mirror: " + e.what());
            }
        }
        if (!loaded && state.status == RepositoryStatus::Available) {
            state.status = RepositoryStatus::Unavailable;
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
