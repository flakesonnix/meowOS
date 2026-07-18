#ifndef MEOWOS_DATABASE_H
#define MEOWOS_DATABASE_H

#include <filesystem>
#include <optional>
#include <vector>

#include <meow/package/package.hpp>
#include <meow/types/types.hpp>

namespace meow::database {
    struct Database {
        void* handle;
    };

    Database openDatabase(const std::filesystem::path& path);
    void closeDatabase(Database& db);
    void initializeDatabase(Database& db);

    void registerPackage(Database& db, const package::PackageFile& package);
    bool isInstalled(Database& db, const types::PackageName& name);
    std::vector<types::PackageName> listInstalled(Database& db);
    std::optional<types::PackageVersion> installedVersion(Database& db, const types::PackageName& name);
}

#endif //MEOWOS_DATABASE_H
