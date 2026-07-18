#ifndef MEOWOS_CONTEXT_H
#define MEOWOS_CONTEXT_H

#include <meow/config/config.hpp>
#include <meow/database/database.hpp>
#include <meow/repository/repository.hpp>

namespace meow {

struct Context {
    config::Config config;
    database::Database database;
    repository::Repository repository;
};

}

#endif
