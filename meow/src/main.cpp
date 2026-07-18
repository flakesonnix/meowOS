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
#include <meow/install/installer.hpp>
#include <meow/dependency/resolver.hpp>
#include <meow/remove/remove.hpp>
#include <meow/upgrade/upgrade.hpp>
#include <meow/lock/lockfile.hpp>
#include <meow/verify/verifier.hpp>
#include <meow/repair/repair.hpp>
#include <meow/sync/sync.hpp>
#include <meow/update/updater.hpp>

namespace {
    const auto lockfilePath = std::filesystem::path("meow.lock");
    const auto installRoot = std::filesystem::path("/tmp/meow-install");

    auto openDb() {
        return meow::database::openDatabase("");
    }

    void cmdInfo(const meow::repository::Repository& repo, std::string_view name) {
        auto pkg = meow::repository::resolvePackage(repo, meow::types::PackageName{std::string(name)});

        std::cout << "Package      " << pkg.metadata.name.value << "\n"
                  << "Version      " << pkg.metadata.version.value << "\n"
                  << "Architecture " << (pkg.metadata.CpuArch == meow::types::CpuArch::AMD64 ? "amd64" : "aarch64") << "\n"
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
                std::cout << "  " << dep.value << "\n";
            }
        }

        std::cout << "\n"
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
}

int main(int argc, char** argv) {
    if (argc < 2) {
    std::cerr << "usage: meow <command> [args]\n"
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
              << "  update [--dry-run]\n";
        return 1;
    }

    try {
        auto cfg = meow::config::defaultConfig();
        auto repo = meow::repository::loadRepository(cfg.repositories[0]);
        auto db = openDb();

        meow::log::log(meow::log::LogLevel::Debug, "config loaded, database opened");

        std::string_view cmd = argv[1];

        if (cmd == "info") {
            if (argc < 3) {
                std::cerr << "usage: meow info <package>\n";
                return 1;
            }
            cmdInfo(repo, argv[2]);
        } else if (cmd == "list") {
            cmdList(repo);
        } else if (cmd == "search") {
            if (argc < 3) {
                std::cerr << "usage: meow search <query>\n";
                return 1;
            }
            cmdSearch(repo, argv[2]);
        } else if (cmd == "install") {
            if (argc < 3) {
                std::cerr << "usage: meow install [--locked] <package>\n";
                return 1;
            }

            bool locked = false;
            std::string pkgName;

            if (argv[2] == std::string_view("--locked")) {
                if (argc < 4) {
                    std::cerr << "usage: meow install --locked <package>\n";
                    return 1;
                }
                locked = true;
                pkgName = argv[3];
            } else {
                pkgName = argv[2];
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
                meta = meow::repository::resolvePackage(repo, meow::types::PackageName{pkgName}).metadata;
                auto tree = meow::dependency::resolveDependencies(repo, meta, db);

                meow::log::log(meow::log::LogLevel::Info, "resolving dependencies");
                for (const auto& name : tree.packages) {
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
            if (argc < 3) {
                std::cerr << "usage: meow upgrade <package>\n";
                return 1;
            }
            meow::upgrade::upgradePackage(repo, db, meow::types::PackageName{argv[2]}, installRoot);
        } else if (cmd == "remove") {
            if (argc < 3) {
                std::cerr << "usage: meow remove <package>\n";
                return 1;
            }
            meow::remove::removePackage(meow::types::PackageName{argv[2]}, db);
        } else if (cmd == "verify") {
            meow::log::log(meow::log::LogLevel::Info, "checking installed packages");
            auto vr = meow::verify::verifyAll(db);
            size_t errors = vr.missing.size() + vr.modified.size();
            if (errors == 0) {
                meow::log::log(meow::log::LogLevel::Info, "all files intact");
            } else {
                meow::log::log(meow::log::LogLevel::Warning,
                    std::to_string(errors) + " error" + (errors == 1 ? "" : "s") + " found");
            }
        } else if (cmd == "repair") {
            if (argc >= 3) {
                meow::log::log(meow::log::LogLevel::Info, std::string("checking ") + argv[2]);
                meow::repair::repairPackage(repo, db, meow::types::PackageName{argv[2]}, installRoot);
            } else {
                meow::log::log(meow::log::LogLevel::Info, "checking installed packages");
                meow::repair::repairAll(repo, db, installRoot);
            }
        } else if (cmd == "sync") {
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
            if (argc >= 3 && argv[2] == std::string_view("--dry-run")) {
                auto updates = meow::sync::checkUpdates(repo, db);
                if (updates.empty()) {
                    meow::log::log(meow::log::LogLevel::Info, "all packages up to date");
                } else {
                    meow::log::log(meow::log::LogLevel::Info, "would update:");
                    for (const auto& u : updates) {
                        std::cout << "  " << u.name.value
                                  << "  " << u.installed.value
                                  << " -> " << u.available.value << "\n";
                    }
                    meow::log::log(meow::log::LogLevel::Info, "no changes made");
                }
            } else {
                meow::update::updateAll(repo, db);
            }
        } else if (cmd == "installed") {
            cmdInstalled(db);
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
