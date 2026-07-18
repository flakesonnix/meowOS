#ifndef MEOWOS_SYNC_H
#define MEOWOS_SYNC_H

#include <vector>

#include <meow/repository/repository.hpp>
#include <meow/database/database.hpp>
#include <meow/types/types.hpp>

namespace meow::sync {

struct PackageUpdate {
    types::PackageName name;
    types::PackageVersion installed;
    types::PackageVersion available;
};

std::vector<PackageUpdate> checkUpdates(
    repository::Repository& repo,
    database::Database& db
);

}

#endif
