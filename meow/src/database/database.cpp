#include <meow/database/database.hpp>
#include <meow/error/error.hpp>
#include <sqlite3.h>
#include <cstdlib>
#include <ctime>

namespace meow::database {
    namespace {
        std::filesystem::path defaultDbPath() {
            const char* home = std::getenv("HOME");
            if (!home) throw error::MeowError(error::ErrorCode::Internal, "HOME not set");
            auto dir = std::filesystem::path(home) / ".local" / "share" / "meow";
            std::filesystem::create_directories(dir);
            return dir / "database.db";
        }

        sqlite3* h(Database& db) { return static_cast<sqlite3*>(db.handle); }
    }

    Database openDatabase(const std::filesystem::path& path) {
        auto dbPath = path.empty() ? defaultDbPath() : path;

        sqlite3* handle;
        int rc = sqlite3_open(dbPath.c_str(), &handle);
        if (rc != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseOpenFailed, sqlite3_errmsg(handle));
        }

        Database db{handle};
        initializeDatabase(db);
        return db;
    }

    void closeDatabase(Database& db) {
        if (db.handle) {
            sqlite3_close(h(db));
            db.handle = nullptr;
        }
    }

    void initializeDatabase(Database& db) {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS packages ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  name TEXT NOT NULL UNIQUE,"
            "  version TEXT NOT NULL,"
            "  architecture TEXT NOT NULL,"
            "  install_time INTEGER NOT NULL"
            ");"
            "CREATE TABLE IF NOT EXISTS files ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  package_id INTEGER NOT NULL,"
            "  path TEXT NOT NULL,"
            "  FOREIGN KEY (package_id) REFERENCES packages(id)"
            ");";

        char* err = nullptr;
        if (sqlite3_exec(h(db), sql, nullptr, nullptr, &err) != SQLITE_OK) {
            std::string msg = err;
            sqlite3_free(err);
            throw error::MeowError(error::ErrorCode::DatabaseMigrationFailed, msg);
        }
    }

    void registerPackage(Database& db, const package::PackageFile& package) {
        auto* handle = h(db);

        auto archStr = package.metadata.CpuArch == types::CpuArch::AMD64 ? "amd64" : "aarch64";
        auto now = std::time(nullptr);

        const char* insertPkg = "INSERT OR REPLACE INTO packages (name, version, architecture, install_time) VALUES (?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(handle, insertPkg, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }

        sqlite3_bind_text(stmt, 1, package.metadata.name.value.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, package.metadata.version.value.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, archStr, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(now));

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }
        sqlite3_finalize(stmt);

        auto packageId = sqlite3_last_insert_rowid(handle);

        const char* insertFile = "INSERT INTO files (package_id, path) VALUES (?, ?);";
        if (sqlite3_prepare_v2(handle, insertFile, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }

        for (const auto& f : package.files.value) {
            sqlite3_bind_int64(stmt, 1, packageId);
            sqlite3_bind_text(stmt, 2, f.c_str(), -1, SQLITE_TRANSIENT);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                sqlite3_finalize(stmt);
                throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
            }
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
    }

    bool isInstalled(Database& db, const types::PackageName& name) {
        const char* sql = "SELECT 1 FROM packages WHERE name = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(h(db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(h(db)));
        }

        sqlite3_bind_text(stmt, 1, name.value.c_str(), -1, SQLITE_TRANSIENT);
        bool exists = sqlite3_step(stmt) == SQLITE_ROW;
        sqlite3_finalize(stmt);
        return exists;
    }

    std::vector<types::PackageName> listInstalled(Database& db) {
        const char* sql = "SELECT name FROM packages ORDER BY name;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(h(db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(h(db)));
        }

        std::vector<types::PackageName> names;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            auto text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            names.push_back(types::PackageName{text ? text : ""});
        }
        sqlite3_finalize(stmt);
        return names;
    }

    std::optional<types::PackageVersion> installedVersion(Database& db, const types::PackageName& name) {
        const char* sql = "SELECT version FROM packages WHERE name = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(h(db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(h(db)));
        }

        sqlite3_bind_text(stmt, 1, name.value.c_str(), -1, SQLITE_TRANSIENT);

        std::optional<types::PackageVersion> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            auto text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            result = types::PackageVersion{text ? text : ""};
        }
        sqlite3_finalize(stmt);
        return result;
    }
}
