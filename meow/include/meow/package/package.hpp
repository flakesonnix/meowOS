//
// Created by lucy on 17.07.26.
//

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
        meow::types::CpuArch CpuArch;
        meow::types::Description description;
        meow::types::Dependencies dependencies;
    };

    struct PackageFile {
        std::filesystem::path archivePath;
        PackageMetadata metadata;
        types::FileList files;
    };

    PackageFile loadPackage(const std::filesystem::path& path);
}

#endif //MEOWOS_PACKAGE_H
