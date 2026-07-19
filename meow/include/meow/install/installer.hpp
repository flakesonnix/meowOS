#ifndef MEOWOS_INSTALLER_H
#define MEOWOS_INSTALLER_H

#include <filesystem>
#include <set>
#include <vector>
#include <meow/package/package.hpp>
#include <meow/database/database.hpp>

namespace meow::install {
    void installPackage(const package::PackageFile& package, const std::filesystem::path& root, database::Database& db);
    void installPackages(const std::vector<package::PackageFile>& packages,
                          const std::set<std::string>& requested,
                          database::InstallReason requestReason,
                          const std::filesystem::path& root,
                          database::Database& db);
}

#endif //MEOWOS_INSTALLER_H
