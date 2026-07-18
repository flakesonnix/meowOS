#ifndef MEOWOS_REMOVE_H
#define MEOWOS_REMOVE_H

#include <meow/database/database.hpp>
#include <meow/types/types.hpp>

namespace meow::remove {
    void removePackage(const types::PackageName& name, database::Database& db);
}

#endif //MEOWOS_REMOVE_H
