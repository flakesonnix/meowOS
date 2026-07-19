#ifndef MEOWOS_PACKAGE_H
#define MEOWOS_PACKAGE_H
#include <filesystem>
#include <string>
#include <vector>

#include <meow/types/types.hpp>

namespace meow::package {
    struct PackageMetadata {
        meow::types::PackageName name;
        meow::types::PackageVersion version;
        meow::types::CpuArch architecture;
        meow::types::Description description;
        std::string license;
        std::string homepage;
        std::string maintainer;
        meow::types::Dependencies dependencies;
        meow::types::Dependencies conflicts;
        meow::types::Dependencies provides;
        meow::types::Dependencies replaces;
    };

    struct PackageFile {
        std::filesystem::path archivePath;
        PackageMetadata metadata;
        types::FileList files;
    };

    PackageFile loadPackage(const std::filesystem::path& path);
}

#endif
