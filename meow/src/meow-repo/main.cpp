#include <meow/repo-builder/repo_builder.hpp>
#include <meow/error/error.hpp>
#include <iostream>
#include <cstring>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: meow-repo <command> [options]\n"
                  << "  add <archive.pkg.tar.zst> [--repo <dir>]\n"
                  << "  remove <package> [--repo <dir>]\n"
                  << "  sync [--repo <dir>]\n"
                  << "  sign --key <keyfile> [--repo <dir>] [--key-id <id>]\n";
        return 1;
    }

    try {
        meow::repo::RepoBuildOptions opts;
        opts.repoDir = ".";

        std::string_view cmd = argv[1];

        auto getFlag = [&](const char* name) -> std::string {
            for (int i = 2; i < argc - 1; ++i) {
                if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
            }
            return "";
        };

        if (auto v = getFlag("--repo"); !v.empty()) opts.repoDir = v;
        if (auto v = getFlag("--key"); !v.empty()) opts.signKey = v;
        if (auto v = getFlag("--key-id"); !v.empty()) opts.signKeyId = v;
        if (auto v = getFlag("--id"); !v.empty()) opts.repoId = v;

        if (cmd == "add") {
            if (argc < 3 || argv[2][0] == '-') {
                std::cerr << "usage: meow-repo add <archive>\n";
                return 1;
            }
            opts.archivePath = argv[2];
            meow::repo::repoAdd(opts);
        } else if (cmd == "remove") {
            if (argc < 3 || argv[2][0] == '-') {
                std::cerr << "usage: meow-repo remove <package>\n";
                return 1;
            }
            opts.pkgName = argv[2];
            meow::repo::repoRemove(opts);
        } else if (cmd == "sync") {
            meow::repo::repoSync(opts);
        } else if (cmd == "sign") {
            if (!opts.signKey) {
                std::cerr << "error: --key required for sign\n";
                return 1;
            }
            meow::repo::repoSigUpdate(opts);
        } else {
            std::cerr << "unknown command: " << cmd << "\n";
            return 1;
        }

        return 0;
    } catch (const meow::error::MeowError& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
