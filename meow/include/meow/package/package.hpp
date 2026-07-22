#ifndef MEOWOS_PACKAGE_H
#define MEOWOS_PACKAGE_H
#include <filesystem>
#include <string>
#include <vector>
#include <optional>

#include <meow/types/types.hpp>

namespace meow::package {
    // Build-time reproducibility metadata from the [build] table.
    struct BuildInfo {
        bool reproducible{true};
        std::optional<long long> sourceDateEpoch; // SOURCE_DATE_EPOCH override
    };

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
        std::vector<meow::types::OptionalDependency> optionalDependencies;
        BuildInfo build;
        std::optional<int> bootstrapStage; // 0=normal, 1=stage1, 2=stage2, 3=final
    };

    struct PackageFile {
        std::filesystem::path archivePath;
        PackageMetadata metadata;
        types::FileList files;
    };

    PackageFile loadPackage(const std::filesystem::path& path);
}

#endif
