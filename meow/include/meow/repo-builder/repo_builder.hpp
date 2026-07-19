#ifndef MEOWOS_REPO_BUILDER_H
#define MEOWOS_REPO_BUILDER_H

#include <filesystem>
#include <string>
#include <optional>

namespace meow::repo {

struct RepoBuildOptions {
    std::filesystem::path repoDir;
    std::optional<std::filesystem::path> archivePath;
    std::string pkgName;
    std::optional<std::filesystem::path> signKey;
    std::string signKeyId = "default";
    std::string repoId = "main";
};

void repoAdd(const RepoBuildOptions& opts);
void repoRemove(const RepoBuildOptions& opts);
void repoSync(const RepoBuildOptions& opts);
void repoSigUpdate(const RepoBuildOptions& opts);

}

#endif
