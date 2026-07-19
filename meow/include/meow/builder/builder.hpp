#ifndef MEOWOS_BUILDER_H
#define MEOWOS_BUILDER_H

#include <filesystem>
#include <string>
#include <optional>
#include <meow/package/package.hpp>

namespace meow::builder {

struct BuildOptions {
    std::filesystem::path sourceDir;
    std::filesystem::path outputDir;
    std::optional<std::filesystem::path> signKey;
    std::string signKeyId = "default";
};

struct BuildResult {
    bool success{false};
    std::filesystem::path archivePath;
    std::filesystem::path sigPath;
};

BuildResult buildPackage(const BuildOptions& opts);

}

#endif
