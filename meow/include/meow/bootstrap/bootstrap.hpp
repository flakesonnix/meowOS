#ifndef MEOWOS_BOOTSTRAP_H
#define MEOWOS_BOOTSTRAP_H

#include <filesystem>
#include <string>
#include <vector>

#include <meow/config/config.hpp>

namespace meow::bootstrap {

struct BootstrapOptions {
    std::filesystem::path root;
    std::vector<std::string> packages;
    bool verbose = false;
    bool quiet = false;
    bool force = false;
};

void bootstrapRootFS(const BootstrapOptions& opts,
                     const meow::config::Config& cfg);
}

#endif //MEOWOS_BOOTSTRAP_H
