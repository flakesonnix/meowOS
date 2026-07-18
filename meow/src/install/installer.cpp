#include <meow/install/installer.hpp>
#include <meow/archive/archive.hpp>

namespace meow::install {
    void installPackage(const package::PackageFile& package, const std::filesystem::path& root, database::Database& db) {
        archive::Archive archive{package.archivePath};
        archive::extractAll(archive, root);
        database::registerPackage(db, package);
    }
}
