#include <meow/install/installer.hpp>
#include <meow/archive/archive.hpp>
#include <meow/transaction/transaction.hpp>
#include <meow/log/logger.hpp>
#include <meow/hooks/runner.hpp>
#include <meow/config/config.hpp>
#include <meow/error/error.hpp>
#include <meow/lock/install_lock.hpp>

#include <filesystem>
#include <fstream>
#include <set>

namespace meow::install {
    namespace {
        const char* hookTypeName(hooks::HookType t) {
            switch (t) {
                case hooks::HookType::PreInstall:  return "pre_install";
                case hooks::HookType::PostInstall: return "post_install";
                case hooks::HookType::PreRemove:   return "pre_remove";
                case hooks::HookType::PostRemove:  return "post_remove";
            }
            return "hook";
        }

        hooks::HookPolicy defaultPolicy() {
            auto cfg = config::defaultConfig();
            hooks::HookPolicy policy;
            policy.timeout = std::chrono::seconds(cfg.hookTimeout);
            policy.allowNetwork = cfg.hookAllowNetwork;
            // Test-only override: MEOW_HOOK_TIMEOUT (seconds).
            if (const char* to = std::getenv("MEOW_HOOK_TIMEOUT")) {
                try { policy.timeout = std::chrono::seconds(std::stoi(to)); } catch (...) {}
            }
            return policy;
        }

        void runHookFor(const std::string& name,
                        hooks::HookType type,
                        const package::PackageFile& pkg,
                        const hooks::HookPolicy& policy) {
            try {
                archive::Archive archive{pkg.archivePath};
                auto script = archive::readPackageScript(archive, name);
                if (script.empty()) return;

                auto staging = std::filesystem::temp_directory_path() / "meow" / "hooks"
                             / pkg.metadata.name.value / hookTypeName(type);
                std::filesystem::create_directories(staging);
                auto scriptPath = staging / (name + ".sh");
                {
                    std::ofstream out(scriptPath);
                    out << script;
                }
                std::filesystem::permissions(scriptPath,
                    std::filesystem::perms::owner_read | std::filesystem::perms::owner_write | std::filesystem::perms::owner_exec);

                log::log(log::LogLevel::Info, "running " + name + " for " + pkg.metadata.name.value);
                hooks::runHook(scriptPath, pkg.metadata.name.value,
                               pkg.metadata.version.value, type, policy);
            } catch (const error::MeowError& err) {
                if (err.code == error::ErrorCode::FileNotFound) return; // no such script
                throw;
            }
        }
    }

    void installPackage(const package::PackageFile& package, const std::filesystem::path& root, database::Database& db) {
        auto policy = defaultPolicy();
        archive::Archive archive{package.archivePath};
        runHookFor("pre_install", hooks::HookType::PreInstall, package, policy);
        auto files = archive::extractPackageContent(archive, root);
        database::registerPackage(db, package, files.value);
        runHookFor("post_install", hooks::HookType::PostInstall, package, policy);
    }

    void installPackages(const std::vector<package::PackageFile>& packages,
                          const std::set<std::string>& requested,
                          database::InstallReason requestReason,
                          const std::filesystem::path& root,
                          database::Database& db) {
        // Serialize against any concurrent mutating operation. The lock lives
        // for the whole extraction + commit window and is released on scope
        // exit (also on throw / crash via the kernel).
        auto lock = lock::InstallLock(lock::defaultInstallLockPath(db.path));

        auto policy = defaultPolicy();
        auto tx = transaction::beginTransaction();

        try {
            for (const auto& pkg : packages) {
                log::log(log::LogLevel::Info, "installing " + pkg.metadata.name.value + " " + pkg.metadata.version.value);

                runHookFor("pre_install", hooks::HookType::PreInstall, pkg, policy);

                archive::Archive archive{pkg.archivePath};
                auto files = archive::extractPackageContent(archive, root);
                transaction::recordExtractedFiles(tx, files);

                runHookFor("post_install", hooks::HookType::PostInstall, pkg, policy);

                transaction::Transaction::PackageEntry entry;
                entry.pkg = pkg;
                entry.installedFiles = files.value;
                // `requested` carries the names the user asked for (either a
                // direct install target or a group member). Everything else in
                // the closure is a transitive dependency.
                entry.reason = requested.count(pkg.metadata.name.value)
                    ? requestReason
                    : database::InstallReason::Dependency;
                tx.packages.push_back(std::move(entry));
            }

            transaction::commitTransaction(tx, db);
            log::log(log::LogLevel::Info, "transaction committed");
        } catch (...) {
            log::log(log::LogLevel::Error, "transaction failed, rolling back");
            transaction::rollbackTransaction(tx);
            throw;
        }
    }
}
