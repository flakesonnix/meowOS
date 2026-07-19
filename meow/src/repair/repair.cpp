#include <meow/repair/repair.hpp>
#include <meow/verify/verifier.hpp>
#include <meow/repository/resolver.hpp>
#include <meow/archive/archive.hpp>
#include <meow/download/downloader.hpp>
#include <meow/error/error.hpp>
#include <meow/log/logger.hpp>
#include <filesystem>

namespace meow::repair {

RepairResult repairPackage(
    repository::Repository& repo,
    database::Database& db,
    const types::PackageName& name,
    const std::filesystem::path& root
) {
    RepairResult result;

    if (!database::isInstalled(db, name)) {
        throw error::MeowError(
            error::ErrorCode::PackageNotFound,
            name.value + " is not installed"
        );
    }

    auto issues = verify::verifyPackage(db, name);
    if (issues.missing.empty() && issues.modified.empty()) {
        result.ok = true;
        return result;
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

    for (const auto& f : issues.missing) {
        auto pathStr = f.string();
        auto relPath = pathStr.substr(rootStr.size());
        if (relPath.empty()) relPath = f.filename().string();
        if (relPath.front() == '/') relPath = relPath.substr(1);

        archive::Archive archive{pkg.archivePath};
        archive::extractPackageFile(archive, relPath, root);
        database::updateFileHash(db, f);
        result.repaired.push_back(relPath);
    }

    for (const auto& f : issues.modified) {
        auto pathStr = f.string();
        auto relPath = pathStr.substr(rootStr.size());
        if (relPath.empty()) relPath = f.filename().string();
        if (relPath.front() == '/') relPath = relPath.substr(1);

        archive::Archive archive{pkg.archivePath};
        archive::extractPackageFile(archive, relPath, root);
        database::updateFileHash(db, f);
        result.repaired.push_back(relPath);
    }

    return result;
}

RepairResult repairAll(
    repository::Repository& repo,
    database::Database& db,
    const std::filesystem::path& root
) {
    RepairResult combined;
    auto packages = database::listInstalled(db);
    for (const auto& name : packages) {
        auto r = repairPackage(repo, db, name, root);
        if (!r.ok) {
            combined.repaired.insert(combined.repaired.end(),
                r.repaired.begin(), r.repaired.end());
        }
    }
    return combined;
}

}
