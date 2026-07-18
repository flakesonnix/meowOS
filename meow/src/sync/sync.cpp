#include <meow/sync/sync.hpp>
#include <meow/repository/version.hpp>
#include <meow/repository/resolver.hpp>
#include <meow/error/error.hpp>

namespace meow::sync {

std::vector<PackageUpdate> checkUpdates(
    repository::Repository& repo,
    database::Database& db
) {
    std::vector<PackageUpdate> updates;
    auto packages = database::listInstalled(db);

    for (const auto& name : packages) {
        auto installed = database::installedVersion(db, name);
        if (!installed) continue;

        const auto* repoPkg = repository::findPackage(repo, name);
        if (!repoPkg) continue;

        const auto* latest = repository::latestVersion(*repoPkg);
        if (!latest) continue;

        if (repository::compareVersions(*latest, *installed) > 0) {
            PackageUpdate update;
            update.name = name;
            update.installed = *installed;
            update.available = *latest;
            updates.push_back(std::move(update));
        }
    }

    return updates;
}

}
