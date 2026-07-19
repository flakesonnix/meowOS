#ifndef MEOWOS_CONFIG_H
#define MEOWOS_CONFIG_H

#include <filesystem>
#include <string>
#include <vector>

namespace meow::config {

// A single configured repository source. `id` is a user-friendly config
// name (may be renamed freely); it is NOT the cryptographic repository
// identity (repository.toml's repository_id), which is discovered when the
// repo is loaded.
struct RepositoryConfig {
    std::string id;
    std::string url;   // file://, local path, or http(s):// (scheme selects backend)
    int priority = 0;  // higher priority repos win when a package exists in several
};

struct Config {
    std::filesystem::path root;
    std::filesystem::path cache;
    std::filesystem::path database;
    std::vector<RepositoryConfig> repositories;
    int downloadWorkers = 0; // 0 = default (min(hardware_concurrency, 8))
    int hookTimeout = 30;    // seconds; max runtime for a package script
    bool hookAllowNetwork = false; // network policy for hooks (advisory)
};

Config defaultConfig();

// Load configuration from a TOML file. Recognizes:
//   [[repositories]]
//   id = "main"
//   url = "https://repo.meowos.org"
//   priority = 100
// and the legacy single-repo form `repository = "url"`. Returns
// defaultConfig() if the file is missing or has no repository entries.
Config loadConfig(const std::filesystem::path& path);

}

#endif
