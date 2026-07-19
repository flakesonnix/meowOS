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


    struct VersionConstraint {
        std::string op; // >=, <=, >, <, =
        PackageVersion version;
    };

    struct Dependency {
        PackageName name;
        std::vector<VersionConstraint> constraints;
    };

    struct Dependencies {
        std::vector<Dependency> value;
    };

    struct FileList {
        std::vector<std::filesystem::path> value;
    };

    struct PackageArtifact {
        std::string filename;
        std::string url;
        std::string sha256;
    };
}

#endif
