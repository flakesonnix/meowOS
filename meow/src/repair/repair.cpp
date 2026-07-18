#include <meow/repair/repair.hpp>
#include <meow/verify/verifier.hpp>
#include <meow/repository/resolver.hpp>
#include <meow/archive/archive.hpp>
#include <meow/download/downloader.hpp>
#include <meow/error/error.hpp>
#include <iostream>
#include <filesystem>

namespace meow::repair {

void repairPackage(
    repository::Repository& repo,
    database::Database& db,
    const types::PackageName& name,
    const std::filesystem::path& root
) {
    if (!database::isInstalled(db, name)) {
        throw error::MeowError(
            error::ErrorCode::PackageNotFound,
            name.value + " is not installed"
        );
    }

    auto issues = verify::verifyPackage(db, name);
    size_t total = issues.missing.size() + issues.modified.size();

    if (total == 0) {
        std::cout << "  " << name.value << " OK\n";
        return;
    }

    std::cout << "Found:\n";
    for (const auto& f : issues.missing) {
        std::cout << "  \x1b[31m\u2717 " << f.string() << " (missing)\x1b[0m\n";
    }
    for (const auto& f : issues.modified) {
        std::cout << "  \x1b[33m\u2717 " << f.string() << " (modified)\x1b[0m\n";
    }

    auto installedVer = database::installedVersion(db, name);
    if (!installedVer) {
        throw error::MeowError(
            error::ErrorCode::Internal,
            "inconsistent state: package " + name.value + " installed but no version found"
        );
    }

    auto pkg = repository::resolvePackage(repo, name, *installedVer);
    auto rootStr = root.string();

    std::cout << "\nRepairing:\n";
    for (const auto& f : issues.missing) {
        auto pathStr = f.string();
        auto relPath = pathStr.substr(rootStr.size());
        if (relPath.empty()) relPath = f.filename().string();
        if (relPath.front() == '/') relPath = relPath.substr(1);

        archive::Archive archive{pkg.archivePath};
        archive::extractFile(archive, relPath, root);
        database::updateFileHash(db, f);
        std::cout << "  \x1b[32m\u2713 " << relPath << "\x1b[0m\n";
    }

    for (const auto& f : issues.modified) {
        auto pathStr = f.string();
        auto relPath = pathStr.substr(rootStr.size());
        if (relPath.empty()) relPath = f.filename().string();
        if (relPath.front() == '/') relPath = relPath.substr(1);

        archive::Archive archive{pkg.archivePath};
        archive::extractFile(archive, relPath, root);
        database::updateFileHash(db, f);
        std::cout << "  \x1b[32m\u2713 " << relPath << "\x1b[0m\n";
    }
}

void repairAll(
    repository::Repository& repo,
    database::Database& db,
    const std::filesystem::path& root
) {
    auto packages = database::listInstalled(db);
    for (const auto& name : packages) {
        std::cout << name.value << ":\n";
        repairPackage(repo, db, name, root);
    }
}

}
