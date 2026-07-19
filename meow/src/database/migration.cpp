#include <meow/database/migration.hpp>
#include <meow/database/database.hpp>
#include <meow/error/error.hpp>
#include <sstream>

namespace meow::database {

void migrateDatabase(Database& db, int fromVersion, int toVersion) {
    if (fromVersion == toVersion) return;

    std::ostringstream msg;
    msg << "database migration required: " << fromVersion << " -> " << toVersion;
    throw error::MeowError(error::ErrorCode::DatabaseMigrationFailed, msg.str());
}

}
