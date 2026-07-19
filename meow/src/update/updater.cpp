#include <meow/update/updater.hpp>
#include <meow/sync/sync.hpp>
#include <meow/upgrade/upgrade.hpp>
#include <meow/error/error.hpp>
#include <meow/log/logger.hpp>

namespace meow::update {

UpdateResult updateAll(
    repository::Repository& repo,
    database::Database& db
) {
    UpdateResult result;
    auto updates = sync::checkUpdates(repo, db);

    if (updates.empty()) {
        return result;
    }

    for (const auto& u : updates) {
        try {
            log::log(log::LogLevel::Info, "updating " + u.name.value + " " + u.installed.value + " -> " + u.available.value);
            auto ur = upgrade::upgradePackage(repo, db, u.name, "/tmp/meow-install");
            if (ur.success) {
                result.updated.push_back(u.name);
            }
        } catch (const error::MeowError& e) {
            log::log(log::LogLevel::Error,
                "failed to update " + u.name.value + ": " + e.what());
            result.failed.push_back({u.name, e.what()});
        }
    }

    return result;
}

}
