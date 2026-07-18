#ifndef MEOWOS_UPDATER_H
#define MEOWOS_UPDATER_H

#include <meow/repository/repository.hpp>
#include <meow/database/database.hpp>

namespace meow::update {

void updateAll(
    repository::Repository& repo,
    database::Database& db
);

}

#endif
