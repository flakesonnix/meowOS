#include <iostream>
#include <string_view>
#include <vector>

#include <meow/error/error.hpp>
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
        std::cout << "  created meow.lock\n";
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
              << "  repair [<package>]\n";
        return 1;
    }

    try {
        auto repo = meow::repository::loadRepository("./repo");
        auto db = openDb();

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
                std::cout << "Using lockfile\n\n";
                lock = meow::lock::loadLockfile(lockfilePath);

                const auto* lockedPkg = meow::lock::findLockedPackage(lock, meow::types::PackageName{pkgName});
                if (!lockedPkg) {
                    std::cerr << "package not found in lockfile: " << pkgName << "\n";
                    return 1;
                }
                meta = meow::repository::resolveLockedPackage(lock, meow::types::PackageName{pkgName}).metadata;
                auto tree = meow::dependency::resolveDependencies(repo, meta, db, &lock);

                std::cout << "Resolving dependencies from lockfile...\n\n";
                for (const auto& name : tree.packages) {
                    auto pkg = meow::repository::resolveLockedPackage(lock, name);
                    std::cout << "  " << name.value << " " << pkg.metadata.version.value << "\n";
                    toInstall.push_back(std::move(pkg));
                }
            } else {
                meta = meow::repository::resolvePackage(repo, meow::types::PackageName{pkgName}).metadata;
                auto tree = meow::dependency::resolveDependencies(repo, meta, db);

                std::cout << "Resolving dependencies...\n\n";
                for (const auto& name : tree.packages) {
                    auto pkg = meow::repository::resolvePackage(repo, name);
                    std::cout << "  " << name.value << " " << pkg.metadata.version.value << "\n";
                    toInstall.push_back(std::move(pkg));
                }
            }

            std::cout << "\nInstalling...\n";
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
        } else if (cmd == "repair") {
            if (argc >= 3) {
                std::cout << "Checking " << argv[2] << "...\n\n";
                meow::repair::repairPackage(repo, db, meow::types::PackageName{argv[2]}, installRoot);
            } else {
                std::cout << "Checking installed packages...\n\n";
                meow::repair::repairAll(repo, db, installRoot);
            }
        } else if (cmd == "verify") {
            std::cout << "Checking installed packages...\n\n";
            auto vr = meow::verify::verifyAll(db);
            size_t errors = vr.missing.size() + vr.modified.size();
            if (errors == 0) {
                std::cout << "\nAll files intact\n";
            } else {
                std::cout << "\n" << errors << " error" << (errors == 1 ? "" : "s") << " found\n";
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
