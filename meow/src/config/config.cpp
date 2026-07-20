#include <meow/config/config.hpp>
#include <meow/log/logger.hpp>
#include <meow/error/error.hpp>

#include <cstdlib>
#include <set>

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

    if (auto e = tbl["resolver"]["engine"].value<std::string>()) {
        cfg.resolverEngine = parseResolverEngine(*e);
    }

    if (auto s = tbl["security"]["require_repository_signature"].value<bool>()) {
        cfg.requireRepositorySignature = *s;
    }

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

    // Package groups: [[groups]] name = "x" packages = ["a", "b"]. Groups are
    // local policy aliases, not package identities. Validation is strict: a
    // group with no name, no members, or a name clashing with a reserved CLI
    // command (or another group) is rejected so the CLI never becomes ambiguous.
    if (auto* groupsArr = tbl["groups"].as_array()) {
        std::set<std::string> seen;
        static const char* const reserved[] = {
            "install",  "remove", "update",  "upgrade", "list",    "sync",
            "search",   "info",   "clean",   "verify",   "repair",  "owns",
            "required-by", "installed", "keys", "doctor", "group", nullptr};
        for (const auto& node : *groupsArr) {
            if (auto* t = node.as_table()) {
                PackageGroup g;
                g.name = (*t)["name"].value_or("");
                if (g.name.empty())
                    throw error::MeowError(error::ErrorCode::InvalidRepository,
                                           "group with empty name");
                for (const auto& r : reserved) {
                    if (r && g.name == r)
                        throw error::MeowError(
                            error::ErrorCode::InvalidRepository,
                            "group name conflicts with reserved command: " + g.name);
                }
                if (!seen.insert(g.name).second)
                    throw error::MeowError(error::ErrorCode::InvalidRepository,
                                           "duplicate group name: " + g.name);
                if (auto* pkgs = (*t)["packages"].as_array()) {
                    for (const auto& p : *pkgs) {
                        if (auto s = p.value<std::string>())
                            if (!s->empty()) g.packages.push_back(*s);
                    }
                }
                if (g.packages.empty())
                    throw error::MeowError(error::ErrorCode::InvalidRepository,
                                           "group '" + g.name + "' has no packages");
                cfg.groups.push_back(std::move(g));
            }
        }
    }

    return cfg;
}

ResolverEngine parseResolverEngine(const std::string& s) {
    if (s == "legacy") return ResolverEngine::Legacy;
    if (s == "sat") return ResolverEngine::Sat;
    return ResolverEngine::Auto;
}

}  // namespace meow::config
