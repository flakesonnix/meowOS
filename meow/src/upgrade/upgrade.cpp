#include <meow/upgrade/upgrade.hpp>
#include <meow/repository/resolver.hpp>
#include <meow/repository/version.hpp>
#include <meow/archive/archive.hpp>
#include <meow/transaction/transaction.hpp>
#include <meow/error/error.hpp>
#include <meow/log/logger.hpp>
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
            log::log(log::LogLevel::Info, "upgrading " + name.value + " " + installedVer->value + " -> " + latest->value);

            log::log(log::LogLevel::Info, "removing old files");
            std::error_code ec;
            for (const auto& f : oldFiles) {
                std::filesystem::remove(f, ec);
            }

            log::log(log::LogLevel::Info, "installing " + latest->value);
            archive::Archive archive{latestPkg.archivePath};
            auto newFiles = archive::extractPackageContent(archive, root);
            transaction::recordExtractedFiles(tx, newFiles);

            transaction::Transaction::PackageEntry entry;
            entry.pkg = std::move(latestPkg);
            entry.installedFiles = newFiles.value;
            tx.packages.push_back(std::move(entry));
            transaction::commitTransaction(tx, db);
            log::log(log::LogLevel::Info, "upgrade complete");
        } catch (...) {
            log::log(log::LogLevel::Error, "upgrade failed, rolling back");
            transaction::rollbackTransaction(tx);
            throw;
        }
    }
}
