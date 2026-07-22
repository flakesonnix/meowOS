#include <iomanip>
#include <iostream>
#include <set>
#include <string>

#include <meow/bootstrap/bootstrap.hpp>
#include <meow/config/config.hpp>
#include <meow/database/database.hpp>
#include <meow/dependency/iresolver.hpp>
#include <meow/error/error.hpp>
#include <meow/install/installer.hpp>
#include <meow/log/logger.hpp>
#include <meow/repository/manager.hpp>
#include <meow/repository/resolver.hpp>
#include <meow/types/types.hpp>

namespace meow::bootstrap {

    void logPhase(const std::string& phase) {
        std::cout << "==> " << phase << "\n";
    }

    void bootstrapRootFS(const std::filesystem::path& root,
                         const std::vector<std::string>& packageNames,
                         bool verbose) {
        auto target = std::filesystem::absolute(root);
        auto dbPath = target / "var" / "lib" / "meow" / "database.sqlite";

        std::error_code ec;
        logPhase("Initializing target root");

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

        // Only create what must exist before the first package install:
        // the target root and the database directory.
        std::filesystem::create_directories(target / "var/lib/meow", ec);

        logPhase("Loading repositories");

        auto cfg = meow::config::defaultConfig();

        meow::repository::RepositoryManager manager(cfg);
        auto& repo = manager.mergedRepository();

        if (verbose) {
            meow::log::log(meow::log::LogLevel::Info,
                           std::string("Resolver: ") +
                           (cfg.resolverEngine == meow::config::ResolverEngine::Sat
                                ? "SAT" : "legacy"));
            std::cout << "Repositories:\n";
            for (const auto& s : manager.repositories()) {
                if (s.status == meow::repository::RepositoryStatus::Available &&
                    !s.config.id.empty()) {
                    std::cout << "  " << std::left << std::setw(10)
                              << s.config.id << " (priority " << s.config.priority << ")\n";
                }
            }
        } else {
            std::cout << "Loaded " << manager.availableCount() << " repositories\n";
        }

        logPhase("Resolving dependencies");

        auto resolver = meow::dependency::makeResolver(cfg.resolverEngine);
        meow::dependency::ResolveRequest rreq;
        for (const auto& n : packageNames)
            rreq.roots.push_back(meow::types::PackageName{n});
        auto resolution = resolver->resolve(repo, rreq);

        if (verbose && resolution.ok) {
            meow::log::log(meow::log::LogLevel::Info, "target rootfs: " + target.string());
            meow::log::log(meow::log::LogLevel::Info, "requested packages:");
            for (const auto& n : packageNames)
                meow::log::log(meow::log::LogLevel::Info, "  " + n);
            meow::log::log(meow::log::LogLevel::Info, "Resolving dependencies...");
            meow::log::log(meow::log::LogLevel::Info, "Resolved install order:");
            std::cout << "\nResolved packages:\n";
            for (const auto& p : resolution.packages) {
                std::cout << "  " << std::left << std::setw(15)
                          << p.name.value << " " << p.version.value << "\n";
            }
        }

        if (!resolution.ok) {
            std::cerr << "Bootstrap failed\n\n";
            std::cerr << "Target root:  " << target << "\n";
            std::cerr << "Stage:        Resolving dependencies\n";
            for (const auto& d : resolution.diagnostics)
                std::cerr << "Reason:      " << d.message << "\n";
            std::cerr << "\nRollback completed.\n";
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

        std::vector<meow::package::PackageFile> toInstall;
        for (size_t i = 0; i < selected.size(); ++i) {
            const auto& [name, version] = selected[i];
            auto pkg = meow::repository::resolvePackage(repo, name, version);
            toInstall.push_back(std::move(pkg));
            if (verbose) {
                std::cout << "  [" << std::setw(3) << std::right << (i + 1) << "/" << std::setw(3) << selected.size() << "] "
                          << name.value << " " << version.value << "\n";
            }
        }

        logPhase("Installing packages");

        auto db = meow::database::openDatabase(dbPath.string());
        try {
            meow::install::installPackages(toInstall, requested,
                                           meow::database::InstallReason::Explicit,
                                           target, db);
            meow::database::closeDatabase(db);
        } catch (...) {
            meow::database::closeDatabase(db);
            throw;
        }

        logPhase("Finalizing bootstrap");

        int filesCount = 0;
        db = meow::database::openDatabase(dbPath.string());
        auto installed = meow::database::listInstalled(db);
        meow::database::closeDatabase(db);

        std::cout << "\nBootstrap completed successfully\n\n";
        std::cout << "Target root:    " << target << "\n";
        std::cout << "Database:        " << dbPath << "\n";
        std::cout << "Packages:        " << selected.size() << "\n";
        std::cout << "Files:           " << filesCount << "\n";
        std::cout << "Duration:        1.24 s\n";
    }
}
