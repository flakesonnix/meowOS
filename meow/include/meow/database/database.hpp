#ifndef MEOWOS_DATABASE_H
#define MEOWOS_DATABASE_H

#include <filesystem>
#include <optional>
#include <vector>

#include <meow/package/package.hpp>
#include <meow/types/types.hpp>

namespace meow::database {

// Why a package is present in the database. A package has a single current
// reason; it can only be upgraded, never downgraded (Explicit > GroupMember >
// Dependency). History rows are append-only and preserve the reason at the time
// of each action.
enum class InstallReason {
    Explicit,    // user ran: meow install foo
    GroupMember, // installed through meow group install <group>
    Dependency,  // pulled in by the resolver
};

struct FileEntry {
    std::filesystem::path path;
    std::string sha256;
    int64_t size;
};

// One append-only history record. History is a log of install/remove actions,
// never the source of truth for "what is installed" (that is `packages`).
struct HistoryEntry {
    long long timestamp = 0;  // unix seconds
    std::string action;        // "install" / "remove"
    std::string package;
    std::string version;
    std::string reason;        // string form of InstallReason
    std::string transactionId;
};

struct Database {
    void* handle;
    std::filesystem::path path;
};

Database openDatabase(const std::filesystem::path& path);
void closeDatabase(Database& db);
void initializeDatabase(Database& db);
// Returns true if all expected schema tables are present.
bool checkSchema(Database& db);

// Execute a raw SQL statement (no bindings). Throws on error.
void execRaw(Database& db, const std::string& sql);

    void registerPackage(Database& db, const package::PackageFile& package, const std::vector<std::filesystem::path>& installedFiles);
    void updateFileHash(Database& db, const std::filesystem::path& path);
bool isInstalled(Database& db, const types::PackageName& name);
std::vector<types::PackageName> listInstalled(Database& db);
std::optional<types::PackageVersion> installedVersion(Database& db, const types::PackageName& name);
std::vector<std::filesystem::path> listPackageFiles(Database& db, const types::PackageName& name);
std::vector<FileEntry> listPackageFileEntries(Database& db, const types::PackageName& name);
    void removePackageFiles(Database& db, const types::PackageName& name);
    void removePackageRecord(Database& db, const types::PackageName& name);

std::optional<types::PackageName> owns(Database& db, const std::filesystem::path& filePath);
std::vector<types::PackageName> requiredBy(Database& db, const types::PackageName& name);

// --- Install reason (current state, one row per package) ---

// Set the reason for a package, upgrading only (Explicit > GroupMember >
// Dependency). Never downgrades an existing stronger reason.
void setInstallReason(Database& db, const types::PackageName& name, InstallReason reason);
// Current reason for a package, or nullopt if not installed / unknown.
std::optional<InstallReason> installReason(Database& db, const types::PackageName& name);
// All explicitly-installed packages (reason == Explicit).
std::vector<types::PackageName> explicitlyInstalled(Database& db);

// --- History (append-only log) ---

// Record an install/remove action. History is never edited after insertion.
void recordHistory(Database& db, const std::string& action,
                   const types::PackageName& name, const types::PackageVersion& version,
                   InstallReason reason, const std::string& transactionId);
// All history entries, oldest first.
std::vector<HistoryEntry> packageHistory(Database& db);
// History entries for a single package, oldest first.
std::vector<HistoryEntry> packageHistory(Database& db, const types::PackageName& name);

}  // namespace meow::database

#endif //MEOWOS_DATABASE_H
