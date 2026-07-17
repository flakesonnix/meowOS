#ifndef COMMON_TYPES_HPP
#define COMMON_TYPES_HPP
#include <filesystem>
#include <string>
#include <vector>


namespace meow::types {
    enum class CpuArch {
        AMD64,
        AARCH64
    };

    struct PackageName {
        std::string value;
    };

    struct PackageVersion {
        std::string value;
    };


    struct Architecture {
        CpuArch value;
    };

    struct Description {
        std::string value;
    };


    struct Dependencies {
        std::vector<PackageName> value;
    };

    struct FileList {
        std::vector<std::filesystem::path> value;
    };
}

#endif
