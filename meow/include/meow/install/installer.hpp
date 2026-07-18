#ifndef MEOWOS_INSTALLER_H
#define MEOWOS_INSTALLER_H

#include <filesystem>
#include <meow/package/package.hpp>

namespace meow::install {
    void installPackage(const package::PackageFile& package, const std::filesystem::path& root);
}

#endif //MEOWOS_INSTALLER_H
