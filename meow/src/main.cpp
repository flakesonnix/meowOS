#include <iostream>
#include <string_view>

#include <meow/error/error.hpp>
#include <meow/database/database.hpp>
#include <meow/repository/repository.hpp>
#include <meow/repository/version.hpp>
#include <meow/repository/resolver.hpp>
#include <meow/install/installer.hpp>

namespace {
    auto openDb() {
        auto db = meow::database::openDatabase("");
        return db;
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
}

int main(int argc, char** argv) {
    if (argc < 2) {
    std::cerr << "usage: meow <command> [args]\n"
              << "  info   <package>\n"
              << "  list\n"
              << "  search <query>\n"
              << "  install <package>\n"
              << "  installed\n";
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
                std::cerr << "usage: meow install <package>\n";
                return 1;
            }
            auto pkg = meow::repository::resolvePackage(repo, meow::types::PackageName{argv[2]});
            meow::install::installPackage(pkg, "/tmp/meow-install", db);
            std::cout << "installed " << argv[2] << " to /tmp/meow-install\n";
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
