#include <meow/update/updater.hpp>
#include <meow/sync/sync.hpp>
#include <meow/upgrade/upgrade.hpp>
#include <meow/error/error.hpp>
#include <iostream>

namespace meow::update {

void updateAll(
    repository::Repository& repo,
    database::Database& db
) {
    auto updates = sync::checkUpdates(repo, db);

    if (updates.empty()) {
        std::cout << "All packages up to date\n";
        return;
    }

    std::cout << "Updates available:\n\n";
    for (const auto& u : updates) {
        std::cout << "  " << u.name.value
                  << "  " << u.installed.value
                  << " -> " << u.available.value << "\n";
    }

    std::cout << "\n";

    for (const auto& u : updates) {
        std::cout << "Updating " << u.name.value << "...\n";
        upgrade::upgradePackage(repo, db, u.name, "/tmp/meow-install");
        std::cout << "\n";
    }

    std::cout << "Update complete\n";
}

}
