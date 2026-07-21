#ifndef MEOWOS_BOOTSTRAP_H
#define MEOWOS_BOOTSTRAP_H

#include <filesystem>
#include <vector>

namespace meow::bootstrap {
    void createRootFSSkeleton(const std::filesystem::path& root);
    void bootstrapRootFS(const std::filesystem::path& root,
                         const std::vector<std::string>& packageNames);
}

#endif //MEOWOS_BOOTSTRAP_H
