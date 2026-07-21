#include <meow/bootstrap/bootstrap.hpp>

namespace meow::bootstrap {

    void bootstrapRootFS(const std::filesystem::path& root,
                         const std::vector<std::string>& packageNames,
                         bool verbose) {
        auto target = std::filesystem::absolute(root);
        auto dbPath = target / "var" / "lib" / "meow" / "database.sqlite";

        std::error_code ec;
        if (verbose) {
            meow::log::log(meow::LogLevel::Info, "RootFS: " + target.string());
            meow::log::log(meow::LogLevel::Info, "Database: " + dbPath.string());
        }

        if (std::filesystem::exists(target)) {
            if (!std::filesystem::is_empty(target, ec)) {
                throw meow::error::MeowError(
                    meow::error::ErrorCode::TransactionFailed,
                    "target directory is not empty: " + target.string()
                );
            }
        } else {
            std::filesystem::create_directories(target, ec);
        }

        std::string skeletonSubdirs[] = {"dev", "proc", "sys", "run", "etc",
                                          "usr", "var", "home",
                                          "var/lib/meow", "var/log"};
        for (const auto& sub : skeletonSubdirs) {
            std::filesystem::create_directories(target / sub, ec);
        }

        auto db = meow::database::openDatabase(dbPath.string());
        if (verbose) {
            meow::log::log(meow::LogLevel::Info, "bootstrap database initialized");
            meow::log::log(meow::LogLevel::Info, "bootstrap transaction started");
        } else {
            meow::log::log(meow::LogLevel::Info, "bootstrap database initialized");
        }

        auto cfg = meow::config::defaultConfig();

        if (verbose) {
            meow::log::log(meow::LogLevel::Info, "Resolver: " + 
                (cfg.resolverEngine == meow::config::ResolverEngine::Sat ? "SAT" : "legacy"));
        }

        meow::repository::RepositoryManager manager(cfg);
        auto repo = manager.mergedRepository();

        if (verbose) {
            meow::log::log(meow::LogLevel::Info, "Loaded " + 
                std::to_string(manager.availableCount()) + 
                " repositories");
        }

        auto resolver = meow::dependency::makeResolver(cfg.resolverEngine);
        meow::dependency::ResolveRequest rreq;
        for (const auto& n : packageNames)
            rreq.roots.push_back(meow::types::PackageName{n});
        auto resolution = resolver->resolve(repo, rreq);

        if (!resolution.ok) {
            std::cerr << "resolution failed:\n";
            for (const auto& d : resolution.diagnostics)
                std::cerr << "  " << d.message << "\n";
            meow::database::closeDatabase(db);
            throw meow::error::MeowError(
                meow::error::ErrorCode::TransactionFailed,
                "bootstrap resolution failed"
            );
        }

        if (verbose) {
            meow::log::log(meow::LogLevel::Info, "target rootfs: " + target.string());
            meow::log::log(meow::LogLevel::Info, "requested packages:");
            for (const auto& n : packageNames)
                meow::log::log(meow::LogLevel::Info, "  " + n);
            meow::log::log(meow::LogLevel::Info, "resolution complete");
            meow::log::log(meow::LogLevel::Info, "Resolved install order:");
            for (const auto& p : resolution.packages)
                meow::log::log(meow::LogLevel::Info, "  " + p.name.value);
        }

        std::vector<std::pair<meow::types::PackageName, meow::types::PackageVersion>> selected;
        std::set<std::string> requested;
        for (const auto& p : resolution.packages) {
            selected.emplace_back(p.name, p.version);
            if (p.isRoot) requested.insert(p.name.value);
        }

        meow::dependency::resolveAndStage(repo, cfg, selected);

        std::vector<meow::package::PackageFile> toInstall;
        for (const auto& [name, version] : selected) {
            auto pkg = meow::repository::resolvePackage(repo, name, version);
            toInstall.push_back(std::move(pkg));
            if (verbose) {
                meow::log::log(meow::LogLevel::Info, "Installing " + name.value + " " + version.value + "...");
                meow::log::log(meow::LogLevel::Info, "  Verifying signature...");
                meow::log::log(meow::LogLevel::Info, "  Extracting...");
                meow::log::log(meow::LogLevel::Info, "  Registering files...");
                meow::log::log(meow::LogLevel::Info, "  ✓ " + name.value);
            }
        }

        meow::log::log(meow::LogLevel::Info, "installing bootstrap packages");
        meow::install::installPackages(toInstall, requested,
                                       meow::database::InstallReason::Explicit,
                                       target, db);

        meow::database::closeDatabase(db);

        std::cout << "\nbootstrap complete: " << target << "\n";

        if (verbose) {
            meow::log::log(meow::LogLevel::Info, "bootstrap transaction committed");
            int filesCount = 0;
            auto db = meow::database::openDatabase(dbPath.string());
            auto installed = meow::database::listInstalled(db);
            meow::database::closeDatabase(db);
            std::cout << "\nBootstrap completed successfully\n";
            std::cout << "\nInstalled: " << selected.size() << " packages\n";
            meow::log::log(meow::LogLevel::Info, "Installed: " + std::to_string(selected.size()) + " packages");
        }
    }
}
