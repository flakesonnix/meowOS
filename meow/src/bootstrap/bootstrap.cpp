#include <meow/bootstrap/bootstrap.hpp>

namespace meow::bootstrap {
    namespace {
        const std::vector<std::string> skeletonSubdirs = {
            "dev", "proc", "sys", "run", "etc",
            "usr", "var", "home",
            "var/lib/meow", "var/log"
        };
    }

    void createRootFSSkeleton(const std::filesystem::path& root) {
        std::error_code ec;
        for (const auto& sub : skeletonSubdirs) {
            std::filesystem::create_directories(root / sub, ec);
        }
    }

    void bootstrapRootFS(const std::filesystem::path& root,
                         const std::vector<std::string>& packageNames) {
        auto target = std::filesystem::absolute(root);

        std::error_code ec;
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

        createRootFSSkeleton(target);

        auto dbPath = target / "var" / "lib" / "meow" / "database.sqlite";
        auto db = meow::database::openDatabase(dbPath.string());
        meow::database::initializeDatabase(db);

        meow::log::log(meow::LogLevel::Info, "bootstrap database initialized");

        auto cfg = meow::config::defaultConfig();
        auto repo = meow::repository::openRepository(".");

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
        }

        meow::log::log(meow::log::LogLevel::Info, "installing bootstrap packages");
        meow::install::installPackages(toInstall, requested,
                                       meow::database::InstallReason::Explicit,
                                       target, db);

        meow::database::closeDatabase(db);
        std::cout << "\nbootstrap complete: " << target << "\n";
    }
}
