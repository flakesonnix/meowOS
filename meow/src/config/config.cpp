#include <meow/config/config.hpp>
#include <meow/log/logger.hpp>

#include <cstdlib>

#include <toml++/toml.hpp>

namespace meow::config {

Config defaultConfig() {
    Config cfg;
    cfg.root = "/";
    cfg.cache = std::filesystem::path(std::getenv("HOME")) / ".cache" / "meow";
    cfg.repositories.push_back(RepositoryConfig{"default", "./repo", 0});
    cfg.downloadWorkers = 0;
    cfg.hookTimeout = 30;
    cfg.hookAllowNetwork = false;
    return cfg;
}

Config loadConfig(const std::filesystem::path& path) {
    Config cfg = defaultConfig();

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return cfg;
    }

    auto tbl = toml::parse_file(path.string());

    // Database / root overrides (optional).
    if (auto r = tbl["root"].value<std::string>()) cfg.root = *r;
    if (auto d = tbl["database"].value<std::string>())
        cfg.database = *d;
    if (auto w = tbl["download_workers"].value<int>()) cfg.downloadWorkers = *w;
    if (auto t = tbl["hook_timeout"].value<int>()) cfg.hookTimeout = *t;
    if (auto n = tbl["hook_allow_network"].value<bool>())
        cfg.hookAllowNetwork = *n;

    std::vector<RepositoryConfig> repos;

    // Modern form: [[repositories]]
    if (auto* arr = tbl["repositories"].as_array()) {
        for (const auto& node : *arr) {
            if (auto* t = node.as_table()) {
                RepositoryConfig rc;
                rc.id = (*t)["id"].value_or("");
                rc.url = (*t)["url"].value_or("");
                if (rc.url.empty()) rc.url = (*t)["path"].value_or("");
                rc.priority = (*t)["priority"].value_or(0);
                if (!rc.url.empty()) repos.push_back(std::move(rc));
            }
        }
    }

    // Legacy form: repository = "url"
    if (repos.empty()) {
        if (auto url = tbl["repository"].value<std::string>()) {
            if (!url->empty())
                repos.push_back(RepositoryConfig{"default", *url, 0});
        }
    }

    if (!repos.empty()) {
        cfg.repositories = std::move(repos);
    }
    return cfg;
}

}  // namespace meow::config
