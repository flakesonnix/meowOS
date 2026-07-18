#include <meow/install/installer.hpp>
#include <meow/archive/archive.hpp>
#include <meow/transaction/transaction.hpp>
#include <meow/log/logger.hpp>

namespace meow::install {
    void installPackage(const package::PackageFile& package, const std::filesystem::path& root, database::Database& db) {
        archive::Archive archive{package.archivePath};
        auto files = archive::extractAll(archive, root);
        database::registerPackage(db, package, files.value);
    }

    void installPackages(const std::vector<package::PackageFile>& packages, const std::filesystem::path& root, database::Database& db) {
        auto tx = transaction::beginTransaction();

        try {
            for (const auto& pkg : packages) {
                log::log(log::LogLevel::Info, "installing " + pkg.metadata.name.value + " " + pkg.metadata.version.value);
                archive::Archive archive{pkg.archivePath};
                auto files = archive::extractAll(archive, root);
                transaction::recordExtractedFiles(tx, files);
                transaction::Transaction::PackageEntry entry;
                entry.pkg = pkg;
                entry.installedFiles = files.value;
                tx.packages.push_back(std::move(entry));
            }

            transaction::commitTransaction(tx, db);
            log::log(log::LogLevel::Info, "transaction committed");
        } catch (...) {
            log::log(log::LogLevel::Error, "transaction failed, rolling back");
            transaction::rollbackTransaction(tx);
            throw;
        }
    }
}
