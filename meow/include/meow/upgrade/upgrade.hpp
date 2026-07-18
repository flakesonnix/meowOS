#ifndef MEOWOS_UPGRADE_H
#define MEOWOS_UPGRADE_H

#include <filesystem>
#include <meow/repository/repository.hpp>
#include <meow/database/database.hpp>
#include <meow/types/types.hpp>

namespace meow::upgrade {
    void upgradePackage(
        repository::Repository& repo,
        database::Database& db,
        const types::PackageName& name,
        const std::filesystem::path& root
    );
}

#endif //MEOWOS_UPGRADE_H
