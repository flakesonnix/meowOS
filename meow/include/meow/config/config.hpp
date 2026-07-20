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
//
// A source is one *repository identity* served from one or more *mirrors*
// (transport locations). All mirrors of a source must yield the same
// `repository_id`, signature, and metadata. The cache is keyed by
// `repository_id`, so mirrors of one source share a single cache and are not
// separate repositories.
struct RepositoryConfig {
    std::string id;
    std::vector<std::string> mirrors;  // file://, local path, or http(s):// endpoints
    int priority = 0;  // higher priority repos win when a package exists in several

    // Effective transport endpoints. Falls back to the legacy single `url`
    // when `mirrors` was not specified, preserving backwards compatibility.
    std::vector<std::string> urls() const {
        if (!mirrors.empty()) return mirrors;
        if (!url.empty()) return {url};
        return {};
    }

    // Legacy single-endpoint form. Retained so old configs keep parsing; it is
    // folded into `mirrors` by the config loader.
    std::string url;
};

// A local package group: a named expansion alias over package names. Groups
// are *user/repository policy* (declared in meow.toml), NOT package identities
// and NOT repository metadata. Installing a group expands to its members and
// installs them through the normal resolver/transaction path; the database
// records the individual packages, not a "group" entity.
struct PackageGroup {
    std::string name;
    std::vector<std::string> packages;  // package names (not versions)
};

struct Config {
    std::filesystem::path root;
    std::filesystem::path cache;
    std::filesystem::path database;
    std::vector<RepositoryConfig> repositories;
    std::vector<PackageGroup> groups;
    int downloadWorkers = 0; // 0 = default (min(hardware_concurrency, 8))
    int hookTimeout = 30;    // seconds; max runtime for a package script
    bool hookAllowNetwork = false; // network policy for hooks (advisory)

    // When true, a repository with no `.sig`, an empty `keyId`, or an invalid
    // signature is a hard error rather than a logged warning. Default false
    // preserves the historical warn-and-continue behavior; operators opt in via
    //   [security]
    //   require_repository_signature = true
    bool requireRepositorySignature = false;
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
