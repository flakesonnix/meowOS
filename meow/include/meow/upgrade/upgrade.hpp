#ifndef MEOWOS_UPGRADE_H
#define MEOWOS_UPGRADE_H

#include <filesystem>
#include <optional>
#include <meow/repository/repository.hpp>
#include <meow/database/database.hpp>
#include <meow/types/types.hpp>

namespace meow::upgrade {

struct UpgradeResult {
    bool success{false};
    bool upToDate{false};
    std::optional<types::PackageVersion> oldVersion;
    std::optional<types::PackageVersion> newVersion;
};

UpgradeResult upgradePackage(
    repository::Repository& repo,
    database::Database& db,
    const types::PackageName& name,
    const std::filesystem::path& root
);

}

#endif
