#include <meow/database/database.hpp>
#include <meow/database/migration.hpp>
#include <meow/error/error.hpp>
#include <meow/format/version.hpp>
#include <meow/download/downloader.hpp>
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

    } // anonymous namespace

        void migrateFilesTable(sqlite3* handle) {
            const char* check = "SELECT COUNT(*) FROM pragma_table_info('files') WHERE name='sha256';";
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(handle, check, -1, &stmt, nullptr) != SQLITE_OK) return;
            sqlite3_step(stmt);
            int count = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
            if (count == 0) {
                const char* alter = "ALTER TABLE files ADD COLUMN sha256 TEXT DEFAULT '';";
                char* err = nullptr;
                sqlite3_exec(handle, alter, nullptr, nullptr, &err);
                if (err) sqlite3_free(err);
            }
        }

    Database openDatabase(const std::filesystem::path& path) {
        auto dbPath = path.empty() ? defaultDbPath() : path;

        sqlite3* handle;
        int rc = sqlite3_open(dbPath.c_str(), &handle);
        if (rc != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseOpenFailed, sqlite3_errmsg(handle));
        }

        Database db{handle, dbPath};
        initializeDatabase(db);
        return db;
    }

    void closeDatabase(Database& db) {
        if (h(db)) {
            sqlite3_close(h(db));
            db.handle = nullptr;
        }
    }

    bool checkSchema(Database& db) {
        auto* handle = h(db);
        if (!handle) return false;
        const char* sql =
            "SELECT name FROM sqlite_master "
            "WHERE type='table' AND name IN "
            "('packages','files','package_deps','package_provides','metadata');";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        int found = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) ++found;
        sqlite3_finalize(stmt);
        return found == 5;
    }

    void initializeDatabase(Database& db) {
        auto* handle = h(db);

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
            "  sha256 TEXT DEFAULT '',"
            "  size INTEGER DEFAULT 0,"
            "  FOREIGN KEY (package_id) REFERENCES packages(id) ON DELETE CASCADE"
            ");"
            "CREATE TABLE IF NOT EXISTS package_deps ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  package_id INTEGER NOT NULL,"
            "  dep_name TEXT NOT NULL,"
            "  FOREIGN KEY (package_id) REFERENCES packages(id) ON DELETE CASCADE"
            ");"
            "CREATE TABLE IF NOT EXISTS package_provides ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  package_id INTEGER NOT NULL,"
            "  provide_name TEXT NOT NULL,"
            "  FOREIGN KEY (package_id) REFERENCES packages(id) ON DELETE CASCADE"
            ");"
            "CREATE TABLE IF NOT EXISTS metadata ("
            "  key TEXT PRIMARY KEY,"
            "  value TEXT NOT NULL"
            ");";

        char* err = nullptr;
        if (sqlite3_exec(handle, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            std::string msg = err;
            sqlite3_free(err);
            throw error::MeowError(error::ErrorCode::DatabaseMigrationFailed, msg);
        }

        migrateFilesTable(handle);

        // Initialize or verify schema version
        const char* getVer = "SELECT value FROM metadata WHERE key = 'schema_version';";
        sqlite3_stmt* vstmt;
        int schemaVersion = 0;
        if (sqlite3_prepare_v2(handle, getVer, -1, &vstmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(vstmt) == SQLITE_ROW) {
                auto text = reinterpret_cast<const char*>(sqlite3_column_text(vstmt, 0));
                if (text) schemaVersion = std::stoi(text);
            }
            sqlite3_finalize(vstmt);
        }

        if (schemaVersion == 0) {
            // Fresh database or unversioned — set to current
            const char* setVer = "INSERT OR REPLACE INTO metadata (key, value) VALUES ('schema_version', ?);";
            if (sqlite3_prepare_v2(handle, setVer, -1, &vstmt, nullptr) == SQLITE_OK) {
                auto verStr = std::to_string(format::CurrentDatabaseSchema);
                sqlite3_bind_text(vstmt, 1, verStr.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(vstmt);
                sqlite3_finalize(vstmt);
            }
            schemaVersion = format::CurrentDatabaseSchema;
        }

        if (schemaVersion != format::CurrentDatabaseSchema) {
            throw error::MeowError(error::ErrorCode::DatabaseMigrationFailed,
                "unsupported database schema version: " + std::to_string(schemaVersion));
        }
    }

    void registerPackage(Database& db, const package::PackageFile& package, const std::vector<std::filesystem::path>& installedFiles) {
        auto* handle = h(db);

        auto archStr = package.metadata.architecture == types::CpuArch::AMD64 ? "amd64" : "aarch64";
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

        const char* insertFile = "INSERT INTO files (package_id, path, sha256, size) VALUES (?, ?, ?, ?);";
        if (sqlite3_prepare_v2(handle, insertFile, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }

        auto& fileList = installedFiles.empty() ? package.files.value : installedFiles;
        for (const auto& f : fileList) {
            std::string hash;
            int64_t fileSize = 0;
            if (std::filesystem::exists(f) && std::filesystem::is_regular_file(f)) {
                hash = download::computeFileHash(f);
                fileSize = static_cast<int64_t>(std::filesystem::file_size(f));
            }

            sqlite3_bind_int64(stmt, 1, packageId);
            sqlite3_bind_text(stmt, 2, f.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, hash.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 4, fileSize);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                sqlite3_finalize(stmt);
                throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
            }
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);

        // store dependencies
        const char* insertDep = "INSERT INTO package_deps (package_id, dep_name) VALUES (?, ?);";
        if (sqlite3_prepare_v2(handle, insertDep, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }
        for (const auto& dep : package.metadata.dependencies.value) {
            sqlite3_bind_int64(stmt, 1, packageId);
            sqlite3_bind_text(stmt, 2, dep.name.value.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                sqlite3_finalize(stmt);
                throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
            }
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);

        // store provides
        const char* insertProvide = "INSERT INTO package_provides (package_id, provide_name) VALUES (?, ?);";
        if (sqlite3_prepare_v2(handle, insertProvide, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }
        for (const auto& prov : package.metadata.provides.value) {
            sqlite3_bind_int64(stmt, 1, packageId);
            sqlite3_bind_text(stmt, 2, prov.name.value.c_str(), -1, SQLITE_TRANSIENT);
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

    std::vector<std::filesystem::path> listPackageFiles(Database& db, const types::PackageName& name) {
        const char* sql =
            "SELECT f.path FROM files f "
            "JOIN packages p ON f.package_id = p.id "
            "WHERE p.name = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(h(db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(h(db)));
        }

        sqlite3_bind_text(stmt, 1, name.value.c_str(), -1, SQLITE_TRANSIENT);

        std::vector<std::filesystem::path> paths;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            auto text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (text) paths.emplace_back(text);
        }
        sqlite3_finalize(stmt);
        return paths;
    }

    std::vector<FileEntry> listPackageFileEntries(Database& db, const types::PackageName& name) {
        const char* sql =
            "SELECT f.path, f.sha256, f.size FROM files f "
            "JOIN packages p ON f.package_id = p.id "
            "WHERE p.name = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(h(db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(h(db)));
        }

        sqlite3_bind_text(stmt, 1, name.value.c_str(), -1, SQLITE_TRANSIENT);

        std::vector<FileEntry> entries;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            FileEntry entry;
            auto pathText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (pathText) entry.path = pathText;
            auto hashText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            if (hashText) entry.sha256 = hashText;
            entry.size = sqlite3_column_int64(stmt, 2);
            entries.push_back(std::move(entry));
        }
        sqlite3_finalize(stmt);
        return entries;
    }

    void updateFileHash(Database& db, const std::filesystem::path& path) {
        auto* handle = h(db);

        std::string hash;
        int64_t fileSize = 0;
        if (std::filesystem::exists(path) && std::filesystem::is_regular_file(path)) {
            hash = download::computeFileHash(path);
            fileSize = static_cast<int64_t>(std::filesystem::file_size(path));
        }

        const char* sql = "UPDATE files SET sha256 = ?, size = ? WHERE path = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }

        sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, fileSize);
        sqlite3_bind_text(stmt, 3, path.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }
        sqlite3_finalize(stmt);
    }

    void removePackageRecord(Database& db, const types::PackageName& name) {
        auto* handle = h(db);

        // cascade delete via FK should handle deps/provides/files
        const char* delPkg = "DELETE FROM packages WHERE name = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(handle, delPkg, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }

        sqlite3_bind_text(stmt, 1, name.value.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }
        sqlite3_finalize(stmt);
    }

    std::optional<types::PackageName> owns(Database& db, const std::filesystem::path& filePath) {
        auto* handle = h(db);
        const char* sql =
            "SELECT p.name FROM files f "
            "JOIN packages p ON f.package_id = p.id "
            "WHERE f.path = ? LIMIT 1;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }

        sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_TRANSIENT);

        std::optional<types::PackageName> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            auto text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (text) result = types::PackageName{text};
        }
        sqlite3_finalize(stmt);
        return result;
    }

    std::vector<types::PackageName> requiredBy(Database& db, const types::PackageName& name) {
        auto* handle = h(db);
        const char* sql =
            "SELECT p.name FROM packages p "
            "JOIN package_deps d ON d.package_id = p.id "
            "WHERE d.dep_name = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }

        sqlite3_bind_text(stmt, 1, name.value.c_str(), -1, SQLITE_TRANSIENT);

        std::vector<types::PackageName> result;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            auto text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (text) result.push_back(types::PackageName{text});
        }
        sqlite3_finalize(stmt);
        return result;
    }
}
