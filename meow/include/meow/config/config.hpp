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
};

Config defaultConfig();

}

#endif
