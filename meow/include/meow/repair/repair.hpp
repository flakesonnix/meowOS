#ifndef MEOWOS_REPAIR_H
#define MEOWOS_REPAIR_H

#include <filesystem>
#include <string>
#include <vector>
#include <meow/repository/repository.hpp>
#include <meow/database/database.hpp>
#include <meow/types/types.hpp>

namespace meow::repair {

struct RepairResult {
    bool ok{false};
    std::vector<std::string> repaired;
};

RepairResult repairPackage(
    repository::Repository& repo,
    database::Database& db,
    const types::PackageName& name,
    const std::filesystem::path& root
);

RepairResult repairAll(
    repository::Repository& repo,
    database::Database& db,
    const std::filesystem::path& root
);

}

#endif
