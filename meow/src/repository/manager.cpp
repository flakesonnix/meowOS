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
        try {
            auto backend = createBackend(rc.url);
            Loaded l;
            l.config = rc;
            l.repository = backend->loadRepository();
            loaded_.push_back(std::move(l));
        } catch (const std::exception& e) {
            ++failed_;
            if (lastError_.empty()) {
                if (auto* me = dynamic_cast<const error::MeowError*>(&e))
                    lastError_ = error::formatError(*me);
                else
                    lastError_ = e.what();
            }
            log::log(log::LogLevel::Warning,
                     "repository '" + rc.id + "' (" + rc.url +
                         ") unavailable: " + e.what());
        }
    }
    merged_ = buildMerged();
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
