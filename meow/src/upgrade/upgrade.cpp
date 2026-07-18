#include <meow/upgrade/upgrade.hpp>
#include <meow/repository/resolver.hpp>
#include <meow/repository/version.hpp>
#include <meow/archive/archive.hpp>
#include <meow/transaction/transaction.hpp>
#include <meow/error/error.hpp>
#include <iostream>
#include <filesystem>

namespace meow::upgrade {
    void upgradePackage(
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

        const auto* repoPkg = repository::findPackage(repo, name);
        if (!repoPkg) {
            throw error::MeowError(
                error::ErrorCode::PackageNotFound,
                "package not found in repository: " + name.value
            );
        }

        const auto* latest = repository::latestVersion(*repoPkg);
        if (!latest) {
            throw error::MeowError(
                error::ErrorCode::VersionNotFound,
                "no versions available for: " + name.value
            );
        }

        auto installedVer = database::installedVersion(db, name);
        if (!installedVer) {
            throw error::MeowError(
                error::ErrorCode::Internal,
                "inconsistent state: package " + name.value + " installed but no version found"
            );
        }

        if (repository::compareVersions(*latest, *installedVer) <= 0) {
            std::cout << name.value << " " << installedVer->value
                      << " is already up to date\n";
            return;
        }

        auto latestPkg = repository::resolvePackage(repo, name, *latest);
        auto oldFiles = database::listPackageFiles(db, name);

        auto tx = transaction::beginTransaction();

        try {
            std::cout << "Upgrading " << name.value << "\n\n"
                      << "  " << installedVer->value << " -> " << latest->value << "\n\n";

            std::cout << "  installing " << latest->value << "\n";
            archive::Archive archive{latestPkg.archivePath};
            auto newFiles = archive::extractAll(archive, root);
            transaction::recordExtractedFiles(tx, newFiles);

            std::cout << "  removing old files\n";
            std::error_code ec;
            for (const auto& f : oldFiles) {
                std::filesystem::remove(f, ec);
            }

            tx.packages.push_back(std::move(latestPkg));
            transaction::commitTransaction(tx, db);
            std::cout << "\nUpgrade complete\n";
        } catch (...) {
            std::cerr << "  upgrade failed, rolling back...\n";
            transaction::rollbackTransaction(tx);
            throw;
        }
    }
}
