#ifndef MEOWOS_BOOTSTRAP_H
#define MEOWOS_BOOTSTRAP_H

#include <filesystem>
#include <vector>

namespace meow::bootstrap {
    void bootstrapRootFS(const std::filesystem::path& root,
                         const std::vector<std::string>& packageNames,
                         bool verbose = false);
}

#endif //MEOWOS_BOOTSTRAP_H
