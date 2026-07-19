#include <iostream>
#include <string_view>
#include <vector>

#include <meow/error/error.hpp>
#include <meow/config/config.hpp>
#include <meow/log/logger.hpp>
#include <meow/database/database.hpp>
#include <meow/repository/repository.hpp>
#include <meow/repository/version.hpp>
#include <meow/repository/resolver.hpp>
#include <meow/repository/manager.hpp>
#include <meow/download/queue.hpp>
#include <meow/install/installer.hpp>
#include <meow/dependency/resolver.hpp>
#include <meow/remove/remove.hpp>
#include <meow/upgrade/upgrade.hpp>
#include <meow/lock/lockfile.hpp>
#include <meow/verify/verifier.hpp>
#include <meow/doctor/doctor.hpp>
#include <meow/repair/repair.hpp>
#include <meow/sync/sync.hpp>
#include <meow/update/updater.hpp>
#include <meow/crypto/keystore.hpp>

namespace {
    const auto lockfilePath = std::filesystem::path("meow.lock");
    const auto installRoot = std::filesystem::path("/tmp/meow-install");

    void cmdInfo(const meow::repository::Repository& repo, std::string_view name) {
        auto pkg = meow::repository::resolvePackage(repo, meow::types::PackageName{std::string(name)});

        std::cout << "Package      " << pkg.metadata.name.value << "\n"
                  << "Version      " << pkg.metadata.version.value << "\n"
                  << "Architecture " << (pkg.metadata.architecture == meow::types::CpuArch::AMD64 ? "amd64" : "aarch64") << "\n"
                  << "\n"
                  << "Description\n"
                  << "------------\n"
                  << pkg.metadata.description.value << "\n"
                  << "\n"
                  << "Dependencies\n"
                  << "------------\n";

        if (pkg.metadata.dependencies.value.empty()) {
            std::cout << "(none)\n";
        } else {
            for (const auto& dep : pkg.metadata.dependencies.value) {
                std::cout << "  " << dep.name.value;
                for (const auto& c : dep.constraints) {
                    std::cout << " " << c.op << c.version.value;
                }
                std::cout << "\n";
            }
        }

        std::cout << "\n"
                  << "License: " << pkg.metadata.license << "\n"
                  << "Homepage: " << pkg.metadata.homepage << "\n"
                  << "Maintainer: " << pkg.metadata.maintainer << "\n"
                  << "\n"
                  << "Files\n"
                  << "-----\n";
        for (const auto& f : pkg.files.value) {
            std::cout << "  " << f.string() << "\n";
        }
    }

    void cmdList(const meow::repository::Repository& repo) {
        for (const auto& name : meow::repository::listPackages(repo)) {
            std::cout << name.value << "\n";
        }
    }

    void cmdSearch(const meow::repository::Repository& repo, std::string_view query) {
        for (const auto& name : meow::repository::listPackages(repo)) {
            if (name.value.find(query) != std::string::npos) {
                std::cout << name.value << "\n";
            }
        }
    }

    void cmdInstalled(meow::database::Database& db) {
        auto packages = meow::database::listInstalled(db);
        if (packages.empty()) {
            std::cout << "(no packages installed)\n";
            return;
        }
        for (const auto& pkg : packages) {
            auto ver = meow::database::installedVersion(db, pkg);
            std::cout << pkg.value;
            if (ver) std::cout << " " << ver->value;
            std::cout << "\n";
        }
    }

    void cmdSaveLockfile(
        const std::vector<meow::package::PackageFile>& packages,
        const meow::repository::Repository& repo
    ) {
        meow::lock::Lockfile lock;
        lock.repositoryHash = "dev";

        for (const auto& pkg : packages) {
            const auto* repoPkg = meow::repository::findPackage(repo, pkg.metadata.name);
            if (!repoPkg) continue;

            for (const auto& rv : repoPkg->versions) {
                if (rv.version.value == pkg.metadata.version.value) {
                    meow::lock::LockedPackage lp;
                    lp.name = pkg.metadata.name;
                    lp.version = pkg.metadata.version;
                    lp.artifact = rv.artifact;
                    lock.packages.push_back(std::move(lp));
                    break;
                }
            }
        }

        meow::lock::saveLockfile(lock, lockfilePath);
        meow::log::log(meow::log::LogLevel::Info, "created meow.lock");
    }

    void cmdVerify(meow::database::Database& db) {
        meow::log::log(meow::log::LogLevel::Info, "checking installed packages");
        auto vr = meow::verify::verifyAll(db);
        size_t errors = vr.missing.size() + vr.modified.size();

        if (errors == 0) {
            meow::log::log(meow::log::LogLevel::Info, "all files intact");
        } else {
            for (const auto& f : vr.missing) {
                std::cout << "  \x1b[31m\u2717 " << f.string() << " (missing)\x1b[0m\n";
            }
            for (const auto& f : vr.modified) {
                std::cout << "  \x1b[33m\u2717 " << f.string() << " (modified)\x1b[0m\n";
            }
            meow::log::log(meow::log::LogLevel::Warning,
                std::to_string(errors) + " error" + (errors == 1 ? "" : "s") + " found");
        }
    }
}

int main(int argc, char** argv) {
    // parse global options
    std::string dbPath;
    std::string repositoryOverride;
    std::string configPath;
    int argi = 1;
    while (argi < argc) {
        std::string_view a = argv[argi];
        if (a == "--db-path" && argi + 1 < argc) {
            dbPath = argv[++argi];
            ++argi;
        } else if (a == "--repository" && argi + 1 < argc) {
            repositoryOverride = argv[++argi];
            ++argi;
        } else if (a == "--config" && argi + 1 < argc) {
            configPath = argv[++argi];
            ++argi;
        } else {
            break;
        }
    }

    if (argi >= argc) {
    std::cerr << "usage: meow [--db-path <path>] <command> [args]\n"
              << "  info   <package>\n"
              << "  list\n"
              << "  search <query>\n"
              << "  install [--locked] <package>\n"
              << "  upgrade <package>\n"
              << "  remove <package>\n"
              << "  installed\n"
              << "  verify\n"
              << "  repair [<package>]\n"
              << "  sync\n"
              << "  update [--dry-run]\n"
               << "  owns <file>\n"
               << "  required-by <package>\n"
               << "  keys list\n"
               << "  keys add <file>\n"
               << "  clean\n";
        return 1;
    }

    auto cmd = argv[argi];
    int cmdArgc = argc - argi;
    char** cmdArgv = argv + argi;

    // Handle keys commands early (no repo/db needed)
    if (cmd == std::string_view("keys")) {
        if (cmdArgc < 2) {
            std::cerr << "usage: meow keys list|add <file>\n";
            return 1;
        }
        if (cmdArgv[1] == std::string_view("list")) {
            auto keys = meow::crypto::listTrustedKeys();
            if (keys.empty()) {
                std::cout << "(no trusted keys)\n";
                return 0;
            }
            std::cout << "Trusted keys:\n";
            for (const auto& k : keys) {
                std::cout << "  " << k.id << "\n    " << k.path.string() << "\n";
            }
            return 0;
        }
        if (cmdArgv[1] == std::string_view("add")) {
            if (cmdArgc < 3) {
                std::cerr << "usage: meow keys add <file>\n";
                return 1;
            }
            meow::crypto::addTrustedKey(cmdArgv[2]);
            std::cout << "added key: " << std::filesystem::path(cmdArgv[2]).filename().string() << "\n";
            return 0;
        }
        std::cerr << "unknown keys command: " << cmdArgv[1] << "\n";
        return 1;
    }

    try {
        auto cfg = meow::config::defaultConfig();
        if (!configPath.empty()) {
            cfg = meow::config::loadConfig(configPath);
        }
        if (!repositoryOverride.empty()) {
            cfg.repositories.clear();
            cfg.repositories.push_back(
                meow::config::RepositoryConfig{"default", repositoryOverride, 0});
        }

        meow::repository::RepositoryManager manager(cfg);
        meow::repository::Repository repo = manager.mergedRepository();
        auto db = meow::database::openDatabase(dbPath.empty() ? "" : dbPath);

        // If no repository could be loaded, surface the failure for any
        // command that depends on repository metadata. This keeps a single
        // broken source a loud error while still tolerating failures when at
        // least one healthy repository is available.
        if (manager.repositories().empty() && cmd != "keys" && cmd != "clean") {
            std::cerr << "error: no repository available: "
                      << manager.lastError() << "\n";
            return 1;
        }

        std::string_view cmd = cmdArgv[0];

        // doctor reports every configured repository via the manager; a
        // broken source is surfaced as a check rather than aborting.
        if (cmd == "doctor") {
            meow::log::setLevel(meow::log::LogLevel::Error);
            bool asJson = false;
            bool security = false;
            for (int i = 1; i < cmdArgc; ++i) {
                if (std::string_view(cmdArgv[i]) == "--json") asJson = true;
                else if (std::string_view(cmdArgv[i]) == "--security") security = true;
            }
            if (security) {
                meow::hooks::HookPolicy policy;
                policy.timeout = std::chrono::seconds(cfg.hookTimeout);
                policy.allowNetwork = cfg.hookAllowNetwork;
                auto diag = meow::doctor::diagnoseSecurity(cfg, db, manager, policy);
                if (asJson) meow::doctor::printJson(diag, std::cout);
                else meow::doctor::printReport(diag, std::cout);
                return diag.healthy() ? 0 : 1;
            }
            auto diag = meow::doctor::diagnose(cfg, db, manager);
            if (asJson) {
                meow::doctor::printJson(diag, std::cout);
            } else {
                meow::doctor::printReport(diag, std::cout);
            }
            return diag.healthy() ? 0 : 1;
        }

        meow::log::log(meow::log::LogLevel::Debug, "config loaded, database opened");

        if (cmd == "info") {
            if (cmdArgc < 2) {
                std::cerr << "usage: meow info <package>\n";
                return 1;
            }
            cmdInfo(repo, cmdArgv[1]);
        } else if (cmd == "list") {
            cmdList(repo);
        } else if (cmd == "search") {
            if (cmdArgc < 2) {
                std::cerr << "usage: meow search <query>\n";
                return 1;
            }
            cmdSearch(repo, cmdArgv[1]);
        } else if (cmd == "install") {
            if (cmdArgc < 2) {
                std::cerr << "usage: meow install [--locked] <package>\n";
                return 1;
            }

            bool locked = false;
            std::string pkgName;

            if (cmdArgv[1] == std::string_view("--locked")) {
                if (cmdArgc < 3) {
                    std::cerr << "usage: meow install --locked <package>\n";
                    return 1;
                }
                locked = true;
                pkgName = cmdArgv[2];
            } else {
                pkgName = cmdArgv[1];
            }

            meow::package::PackageMetadata meta;
            std::vector<meow::package::PackageFile> toInstall;
            meow::lock::Lockfile lock;

            if (locked) {
                meow::log::log(meow::log::LogLevel::Info, "using lockfile");
                lock = meow::lock::loadLockfile(lockfilePath);

                const auto* lockedPkg = meow::lock::findLockedPackage(lock, meow::types::PackageName{pkgName});
                if (!lockedPkg) {
                    std::cerr << "package not found in lockfile: " << pkgName << "\n";
                    return 1;
                }
                meta = meow::repository::resolveLockedPackage(lock, meow::types::PackageName{pkgName}).metadata;
                auto tree = meow::dependency::resolveDependencies(repo, meta, db, &lock);

                meow::log::log(meow::log::LogLevel::Info, "resolving dependencies from lockfile");
                for (const auto& name : tree.packages) {
                    auto pkg = meow::repository::resolveLockedPackage(lock, name);
                    std::cout << "  " << name.value << " " << pkg.metadata.version.value << "\n";
                    toInstall.push_back(std::move(pkg));
                }
            } else {
                // Resolve the dependency closure from repository metadata
                // only (no downloads yet), then fetch all artifacts in
                // parallel and finally load/install them serially.
                meow::log::log(meow::log::LogLevel::Info, "resolving dependency names");
                auto names = meow::repository::resolveDependencyNames(
                    repo, meow::types::PackageName{pkgName});

                std::vector<meow::download::DownloadTask> tasks;
                for (const auto& name : names) {
                    const auto* rp = meow::repository::findPackage(repo, name);
                    if (!rp) {
                        std::cerr << "package not found: " << name.value << "\n";
                        return 1;
                    }
                    if (rp->versions.empty()) {
                        std::cerr << "no version for: " << name.value << "\n";
                        return 1;
                    }
                    const auto& rv = rp->versions.back();
                    meow::download::DownloadTask task;
                    task.artifact = rv.artifact;
                    tasks.push_back(std::move(task));
                }

                meow::download::DownloadQueue queue;
                queue.workers = cfg.downloadWorkers;
                meow::log::log(meow::log::LogLevel::Info,
                    "downloading " + std::to_string(tasks.size()) + " artifacts in parallel");
                meow::download::downloadAll(queue, tasks);

                meow::log::log(meow::log::LogLevel::Info, "resolving packages");
                for (const auto& name : names) {
                    auto pkg = meow::repository::resolvePackage(repo, name);
                    std::cout << "  " << name.value << " " << pkg.metadata.version.value << "\n";
                    toInstall.push_back(std::move(pkg));
                }
            }

            meow::log::log(meow::log::LogLevel::Info, "installing packages");
            meow::install::installPackages(toInstall, installRoot, db);

            if (!locked) {
                cmdSaveLockfile(toInstall, repo);
            }

            std::cout << "\ndone\n";
        } else if (cmd == "upgrade") {
            if (cmdArgc < 2) {
                std::cerr << "usage: meow upgrade <package>\n";
                return 1;
            }
            auto result = meow::upgrade::upgradePackage(repo, db, meow::types::PackageName{cmdArgv[1]}, installRoot);
            if (result.upToDate) {
                std::cout << cmdArgv[1] << " " << result.oldVersion->value << " is already up to date\n";
            }
        } else if (cmd == "remove") {
            if (cmdArgc < 2) {
                std::cerr << "usage: meow remove <package>\n";
                return 1;
            }
            meow::remove::removePackage(meow::types::PackageName{cmdArgv[1]}, db);
        } else if (cmd == "verify") {
            cmdVerify(db);
        } else if (cmd == "repair") {
            if (cmdArgc >= 2) {
                meow::log::log(meow::log::LogLevel::Info, std::string("checking ") + cmdArgv[1]);
                auto result = meow::repair::repairPackage(repo, db, meow::types::PackageName{cmdArgv[1]}, installRoot);
                if (result.ok) {
                    std::cout << "  " << cmdArgv[1] << " OK\n";
                } else {
                    for (const auto& f : result.repaired) {
                        std::cout << "  \x1b[32m\u2713 " << f << "\x1b[0m\n";
                    }
                }
            } else {
                meow::log::log(meow::log::LogLevel::Info, "checking installed packages");
                auto result = meow::repair::repairAll(repo, db, installRoot);
                for (const auto& f : result.repaired) {
                    std::cout << "  \x1b[32m\u2713 " << f << "\x1b[0m\n";
                }
            }
        } else if (cmd == "sync") {
            std::cout << "Synchronizing repositories...\n";
            for (const auto& s : manager.repositories()) {
                if (s.status == meow::repository::RepositoryStatus::Available) {
                    std::cout << "  " << s.config.id << "       "
                              << "\x1b[32m\u2713 " << statusLabel(s.status)
                              << "\x1b[0m\n";
                } else {
                    std::string detail =
                        s.error ? meow::error::formatError(*s.error) : "load failed";
                    std::cout << "  " << s.config.id << "    "
                              << "\x1b[31m\u2717 " << statusLabel(s.status)
                              << ": " << detail << "\x1b[0m\n";
                }
            }

            auto updates = meow::sync::checkUpdates(repo, db);
            if (updates.empty()) {
                meow::log::log(meow::log::LogLevel::Info, "all packages up to date");
            } else {
                meow::log::log(meow::log::LogLevel::Info, "updates available");
                for (const auto& u : updates) {
                    std::cout << "  " << u.name.value
                              << "  " << u.installed.value
                              << " → " << u.available.value << "\n";
                }
                meow::log::log(meow::log::LogLevel::Info,
                    std::to_string(updates.size()) + " update" + (updates.size() == 1 ? "" : "s") + " available");
            }
        } else if (cmd == "update") {
            if (cmdArgc >= 2 && cmdArgv[1] == std::string_view("--dry-run")) {
                auto updates = meow::sync::checkUpdates(repo, db);
                if (updates.empty()) {
                    std::cout << "All packages up to date\n";
                } else {
                    std::cout << "Would update:\n";
                    for (const auto& u : updates) {
                        std::cout << "  " << u.name.value
                                  << "  " << u.installed.value
                                  << " -> " << u.available.value << "\n";
                    }
                    std::cout << "No changes made\n";
                }
            } else {
                auto result = meow::update::updateAll(repo, db);
                if (!result.updated.empty()) {
                    std::cout << "Updated:\n";
                    for (const auto& n : result.updated) {
                        std::cout << "  " << n.value << "\n";
                    }
                }
                if (!result.failed.empty()) {
                    for (const auto& f : result.failed) {
                        std::cout << "  \x1b[31m" << f.name.value << " FAILED: " << f.reason << "\x1b[0m\n";
                    }
                }
                if (result.updated.empty() && result.failed.empty()) {
                    std::cout << "All packages up to date\n";
                }
            }
        } else if (cmd == "owns") {
            if (cmdArgc < 2) {
                std::cerr << "usage: meow owns <file>\n";
                return 1;
            }
            auto owner = meow::database::owns(db, std::filesystem::path(cmdArgv[1]));
            if (owner) {
                auto ver = meow::database::installedVersion(db, *owner);
                std::cout << owner->value;
                if (ver) std::cout << " " << ver->value;
                std::cout << "\n";
            } else {
                std::cout << "(no package owns this file)\n";
            }
        } else if (cmd == "required-by") {
            if (cmdArgc < 2) {
                std::cerr << "usage: meow required-by <package>\n";
                return 1;
            }
            auto deps = meow::database::requiredBy(db, meow::types::PackageName{cmdArgv[1]});
            if (deps.empty()) {
                std::cout << "(nothing depends on " << cmdArgv[1] << ")\n";
            } else {
                for (const auto& d : deps) {
                    std::cout << d.value << "\n";
                }
            }
        } else if (cmd == "installed") {
            cmdInstalled(db);
        } else if (cmd == "clean") {
            meow::repository::clearRepositoryCache();
            std::cout << "cache cleared\n";
        } else {
            std::cerr << "unknown command: " << cmd << "\n";
            return 1;
        }
    } catch (const meow::error::MeowError& e) {
        std::cerr << meow::error::formatError(e) << "\n";
        return 1;
    }

    return 0;
}
