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

    void execRaw(Database& db, const std::string& sql) {
        auto* h = static_cast<sqlite3*>(db.handle);
        char* err = nullptr;
        if (sqlite3_exec(h, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            std::string msg = err;
            sqlite3_free(err);
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, msg);
        }
    }

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
            "('packages','files','package_deps','package_provides','package_history','metadata');";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        int found = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) ++found;
        sqlite3_finalize(stmt);
        return found == 6;
    }

    void initializeDatabase(Database& db) {
        auto* handle = h(db);

        const char* sql =
            "CREATE TABLE IF NOT EXISTS packages ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  name TEXT NOT NULL UNIQUE,"
            "  version TEXT NOT NULL,"
            "  architecture TEXT NOT NULL,"
            "  install_time INTEGER NOT NULL,"
            "  install_reason TEXT NOT NULL DEFAULT 'Dependency'"
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
            "CREATE TABLE IF NOT EXISTS package_history ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  timestamp INTEGER NOT NULL,"
            "  action TEXT NOT NULL,"
            "  package TEXT NOT NULL,"
            "  version TEXT,"
            "  reason TEXT,"
            "  transaction_id TEXT"
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
            if (schemaVersion == 1) {
                // Wrap migration in a transaction so a crash mid-way does not
                // leave the database in a half-migrated state.
                const char* beginSQL = "BEGIN IMMEDIATE TRANSACTION;";
                sqlite3_exec(handle, beginSQL, nullptr, nullptr, nullptr);
                bool began = true;
                try {
                    const char* alterPkg =
                        "ALTER TABLE packages ADD COLUMN install_reason TEXT NOT NULL DEFAULT 'Dependency';";
                    char* merr = nullptr;
                    sqlite3_exec(handle, alterPkg, nullptr, nullptr, &merr);
                    if (merr) sqlite3_free(merr);
                    const char* mkHist =
                        "CREATE TABLE IF NOT EXISTS package_history ("
                        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                        "  timestamp INTEGER NOT NULL,"
                        "  action TEXT NOT NULL,"
                        "  package TEXT NOT NULL,"
                        "  version TEXT,"
                        "  reason TEXT,"
                        "  transaction_id TEXT"
                        ");";
                    char* herr = nullptr;
                    sqlite3_exec(handle, mkHist, nullptr, nullptr, &herr);
                    if (herr) sqlite3_free(herr);
                    // A v1 database may predate the files / deps / provides tables
                    // entirely. Create them idempotently so the schema is complete
                    // and checkSchema() passes after migration.
                    const char* mkMissing =
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
                        ");";
                    char* merr2 = nullptr;
                    sqlite3_exec(handle, mkMissing, nullptr, nullptr, &merr2);
                    if (merr2) sqlite3_free(merr2);
                    const char* setVer =
                        "INSERT OR REPLACE INTO metadata (key, value) VALUES ('schema_version', ?);";
                    if (sqlite3_prepare_v2(handle, setVer, -1, &vstmt, nullptr) == SQLITE_OK) {
                        auto verStr = std::to_string(format::CurrentDatabaseSchema);
                        sqlite3_bind_text(vstmt, 1, verStr.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_step(vstmt);
                        sqlite3_finalize(vstmt);
                    }
                    sqlite3_exec(handle, "COMMIT;", nullptr, nullptr, nullptr);
                    began = false;
                } catch (...) {
                    if (began) sqlite3_exec(handle, "ROLLBACK;", nullptr, nullptr, nullptr);
                    throw;
                }
            } else {
                throw error::MeowError(error::ErrorCode::DatabaseMigrationFailed,
                    "unsupported database schema version: " + std::to_string(schemaVersion));
            }
        }
    }

    void registerPackage(Database& db, const package::PackageFile& package, const std::vector<std::filesystem::path>& installedFiles) {
        auto* handle = h(db);

        auto archStr = package.metadata.architecture == types::CpuArch::AMD64 ? "amd64" : "aarch64";
        auto now = std::time(nullptr);

        // INSERT ... ON CONFLICT preserves the existing `install_reason`
        // column (set separately via setInstallReason), whereas INSERT OR
        // REPLACE would wipe it back to the 'Dependency' default on upgrade.
        const char* insertPkg =
            "INSERT INTO packages (name, version, architecture, install_time) "
            "VALUES (?, ?, ?, ?) "
            "ON CONFLICT(name) DO UPDATE SET "
            "  version = excluded.version, "
            "  architecture = excluded.architecture, "
            "  install_time = excluded.install_time;";
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

        // SELECT the id explicitly — sqlite3_last_insert_rowid does not
        // reliably return the existing rowid when the UPSERT fires a DO
        // UPDATE, and the behavior varies across SQLite versions.
        int64_t packageId = 0;
        {
            const char* getId = "SELECT id FROM packages WHERE name = ?;";
            sqlite3_stmt* idStmt;
            if (sqlite3_prepare_v2(handle, getId, -1, &idStmt, nullptr) != SQLITE_OK) {
                throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
            }
            sqlite3_bind_text(idStmt, 1, package.metadata.name.value.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(idStmt) != SQLITE_ROW) {
                sqlite3_finalize(idStmt);
                throw error::MeowError(error::ErrorCode::Internal, "package not found after INSERT");
            }
            packageId = sqlite3_column_int64(idStmt, 0);
            sqlite3_finalize(idStmt);
        }

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

    void removePackageFiles(Database& db, const types::PackageName& name) {
        auto* handle = h(db);
        const char* delFiles =
            "DELETE FROM files WHERE package_id = (SELECT id FROM packages WHERE name = ?);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(handle, delFiles, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }
        sqlite3_bind_text(stmt, 1, name.value.c_str(), -1, SQLITE_TRANSIENT);
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

        // Files are stored as absolute paths (e.g. /rootfs/usr/bin/gcc).
        // Accept both absolute and relative queries. Use LIKE with a trailing
        // wildcard so that a query like /usr/bin/gcc matches the stored path
        // regardless of the install root prefix.
        const char* sql =
            "SELECT p.name FROM files f "
            "JOIN packages p ON f.package_id = p.id "
            "WHERE f.path = ? OR f.path LIKE ? LIMIT 1;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }

        auto pathStr = filePath.string();
        auto likeStr = "%" + pathStr;
        sqlite3_bind_text(stmt, 1, pathStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, likeStr.c_str(), -1, SQLITE_TRANSIENT);

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

    namespace {
        const char* reasonString(InstallReason r) {
            switch (r) {
                case InstallReason::Explicit:    return "Explicit";
                case InstallReason::GroupMember: return "GroupMember";
                case InstallReason::Dependency:  return "Dependency";
            }
            return "Dependency";
        }
        // Priority: higher number wins (Explicit > GroupMember > Dependency).
        int reasonRank(InstallReason r) {
            switch (r) {
                case InstallReason::Explicit:    return 3;
                case InstallReason::GroupMember: return 2;
                case InstallReason::Dependency:  return 1;
            }
            return 1;
        }
        std::optional<InstallReason> parseReason(const std::string& s) {
            if (s == "Explicit")    return InstallReason::Explicit;
            if (s == "GroupMember") return InstallReason::GroupMember;
            if (s == "Dependency")  return InstallReason::Dependency;
            return std::nullopt;
        }
    }  // namespace

    void setInstallReason(Database& db, const types::PackageName& name, InstallReason reason) {
        auto* handle = h(db);
        // Upgrade only: read current reason, keep the stronger one.
        auto current = installReason(db, name);
        if (current && reasonRank(*current) >= reasonRank(reason)) return;

        const char* sql = "UPDATE packages SET install_reason = ? WHERE name = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }
        sqlite3_bind_text(stmt, 1, reasonString(reason), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, name.value.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }
        sqlite3_finalize(stmt);
    }

    std::optional<InstallReason> installReason(Database& db, const types::PackageName& name) {
        auto* handle = h(db);
        const char* sql = "SELECT install_reason FROM packages WHERE name = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }
        sqlite3_bind_text(stmt, 1, name.value.c_str(), -1, SQLITE_TRANSIENT);
        std::optional<InstallReason> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            auto text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (text) result = parseReason(text);
        }
        sqlite3_finalize(stmt);
        return result;
    }

    std::vector<types::PackageName> explicitlyInstalled(Database& db) {
        auto* handle = h(db);
        const char* sql = "SELECT name FROM packages WHERE install_reason = 'Explicit' ORDER BY name;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }
        std::vector<types::PackageName> names;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            auto text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (text) names.push_back(types::PackageName{text});
        }
        sqlite3_finalize(stmt);
        return names;
    }

    void recordHistory(Database& db, const std::string& action,
                       const types::PackageName& name, const types::PackageVersion& version,
                       InstallReason reason, const std::string& transactionId) {
        auto* handle = h(db);
        const char* sql =
            "INSERT INTO package_history (timestamp, action, package, version, reason, transaction_id) "
            "VALUES (?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(std::time(nullptr)));
        sqlite3_bind_text(stmt, 2, action.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, name.value.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, version.value.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, reasonString(reason), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, transactionId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }
        sqlite3_finalize(stmt);
    }

    std::vector<HistoryEntry> packageHistory(Database& db) {
        return packageHistory(db, types::PackageName{""});
    }

    std::vector<HistoryEntry> packageHistory(Database& db, const types::PackageName& name) {
        auto* handle = h(db);
        const char* sql =
            "SELECT timestamp, action, package, version, reason, transaction_id "
            "FROM package_history ORDER BY id;";
        if (!name.value.empty()) sql = "SELECT timestamp, action, package, version, reason, transaction_id "
                                       "FROM package_history WHERE package = ? ORDER BY id;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw error::MeowError(error::ErrorCode::DatabaseQueryFailed, sqlite3_errmsg(handle));
        }
        if (!name.value.empty())
            sqlite3_bind_text(stmt, 1, name.value.c_str(), -1, SQLITE_TRANSIENT);
        std::vector<HistoryEntry> out;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            HistoryEntry e;
            e.timestamp = sqlite3_column_int64(stmt, 0);
            if (auto t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))) e.action = t;
            if (auto t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))) e.package = t;
            if (auto t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))) e.version = t;
            if (auto t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4))) e.reason = t;
            if (auto t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5))) e.transactionId = t;
            out.push_back(std::move(e));
        }
        sqlite3_finalize(stmt);
        return out;
    }
}
