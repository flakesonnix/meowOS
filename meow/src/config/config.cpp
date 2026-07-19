#include <meow/config/config.hpp>
#include <meow/log/logger.hpp>

#include <cstdlib>

#include <toml++/toml.hpp>

namespace meow::config {

Config defaultConfig() {
    Config cfg;
    cfg.root = "/";
    cfg.cache = std::filesystem::path(std::getenv("HOME")) / ".cache" / "meow";
    cfg.repositories.push_back(
        RepositoryConfig{.id = "default", .mirrors = {"./repo"}, .url = "./repo"});
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
                rc.priority = (*t)["priority"].value_or(0);

                if (auto* mirrors = (*t)["mirrors"].as_array()) {
                    for (const auto& m : *mirrors) {
                        if (auto s = m.value<std::string>())
                            if (!s->empty()) rc.mirrors.push_back(*s);
                    }
                }
                // Legacy single-endpoint fields fold into the mirror list.
                if (rc.mirrors.empty()) {
                    std::string u = (*t)["url"].value_or("");
                    if (u.empty()) u = (*t)["path"].value_or("");
                    if (!u.empty()) {
                        rc.mirrors.push_back(u);
                        rc.url = u;
                    }
                } else {
                    rc.url = rc.mirrors.front();
                }

                if (!rc.mirrors.empty()) repos.push_back(std::move(rc));
            }
        }
    }

    // Legacy form: repository = "url"
    if (repos.empty()) {
        if (auto url = tbl["repository"].value<std::string>()) {
            if (!url->empty()) {
                RepositoryConfig rc;
                rc.id = "default";
                rc.mirrors = {*url};
                rc.url = *url;
                repos.push_back(std::move(rc));
            }
        }
    }

    if (!repos.empty()) {
        cfg.repositories = std::move(repos);
    }
    return cfg;
}

}  // namespace meow::config
