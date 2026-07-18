#include <meow/config/config.hpp>
#include <cstdlib>

namespace meow::config {

Config defaultConfig() {
    Config cfg;
    cfg.root = "/";
    cfg.cache = std::filesystem::path(std::getenv("HOME")) / ".cache" / "meow";
    cfg.repositories.push_back("./repo");
    return cfg;
}

}
