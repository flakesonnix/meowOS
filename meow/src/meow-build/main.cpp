#include <meow/builder/builder.hpp>
#include <meow/error/error.hpp>
#include <iostream>
#include <cstring>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: meow-build [--sign-key <keyfile>] [--output <dir>] [--key-id <id>] <source-dir>\n";
        return 1;
    }

    try {
        meow::builder::BuildOptions opts;
        opts.outputDir = ".";
        opts.sourceDir = std::filesystem::current_path();

        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--sign-key") == 0 && i + 1 < argc) {
                opts.signKey = argv[++i];
            } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
                opts.outputDir = argv[++i];
            } else if (std::strcmp(argv[i], "--key-id") == 0 && i + 1 < argc) {
                opts.signKeyId = argv[++i];
            } else {
                opts.sourceDir = argv[i];
            }
        }

        auto result = meow::builder::buildPackage(opts);

        std::cout << result.archivePath.filename().string() << "\n";

        return 0;
    } catch (const meow::error::MeowError& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
