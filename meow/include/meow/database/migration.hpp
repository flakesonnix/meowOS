#ifndef MEOWOS_DATABASE_MIGRATION_H
#define MEOWOS_DATABASE_MIGRATION_H

namespace meow::database {

struct Database;

void migrateDatabase(Database& db, int fromVersion, int toVersion);

}

#endif
