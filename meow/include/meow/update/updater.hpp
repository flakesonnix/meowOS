#ifndef MEOWOS_UPDATER_H
#define MEOWOS_UPDATER_H

#include <string>
#include <vector>
#include <meow/repository/repository.hpp>
#include <meow/database/database.hpp>
#include <meow/types/types.hpp>

namespace meow::update {

struct UpdateFailed {
    types::PackageName name;
    std::string reason;
};

struct UpdateResult {
    std::vector<types::PackageName> updated;
    std::vector<UpdateFailed> failed;
};

UpdateResult updateAll(
    repository::Repository& repo,
    database::Database& db
);

}

#endif
