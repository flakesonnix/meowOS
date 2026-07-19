#ifndef MEOWOS_CONFIG_H
#define MEOWOS_CONFIG_H

#include <filesystem>
#include <string>
#include <vector>

namespace meow::config {

struct Config {
    std::filesystem::path root;
    std::filesystem::path cache;
    std::filesystem::path database;
    std::vector<std::string> repositories;
    int downloadWorkers = 0; // 0 = default (min(hardware_concurrency, 8))
    int hookTimeout = 30;    // seconds; max runtime for a package script
    bool hookAllowNetwork = false; // network policy for hooks (advisory)
};

Config defaultConfig();

}

#endif
