#include <meow/install/installer.hpp>
#include <meow/archive/archive.hpp>
#include <iostream>

namespace meow::install {
    void installPackage(const package::PackageFile& package, const std::filesystem::path& root, database::Database& db) {
        archive::Archive archive{package.archivePath};
        archive::extractAll(archive, root);
        database::registerPackage(db, package);
    }

    void installPackages(const std::vector<package::PackageFile>& packages, const std::filesystem::path& root, database::Database& db) {
        for (const auto& pkg : packages) {
            std::cout << "  installing " << pkg.metadata.name.value << " " << pkg.metadata.version.value << "\n";
            installPackage(pkg, root, db);
        }
    }
}
