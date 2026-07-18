#include <meow/package/package.hpp>
#include <meow/package/parser.hpp>
#include <meow/archive/archive.hpp>

namespace meow::package {
    PackageFile loadPackage(const std::filesystem::path& path) {
        auto archive = archive::openArchive(path);
        auto content = archive::readFile(archive, "package.toml");
        auto metadata = parsePackageManifest(content);
        auto files = archive::listFiles(archive);

        return PackageFile{path, metadata, files};
    }
}
