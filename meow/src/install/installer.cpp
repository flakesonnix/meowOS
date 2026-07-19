#include <meow/install/installer.hpp>
#include <meow/archive/archive.hpp>
#include <meow/transaction/transaction.hpp>
#include <meow/log/logger.hpp>

namespace meow::install {
    namespace {
        void runScript(const std::string& name, const package::PackageFile& pkg) {
            try {
                archive::Archive archive{pkg.archivePath};
                auto script = archive::readPackageScript(archive, name);
                if (!script.empty()) {
                    log::log(log::LogLevel::Info, "running " + name + " for " + pkg.metadata.name.value);
                    (void)std::system(script.c_str());
                }
            } catch (...) {
                // script not found in archive, skip
            }
        }
    }

    void installPackage(const package::PackageFile& package, const std::filesystem::path& root, database::Database& db) {
        archive::Archive archive{package.archivePath};
        runScript("pre_install", package);
        auto files = archive::extractPackageContent(archive, root);
        database::registerPackage(db, package, files.value);
        runScript("post_install", package);
    }

    void installPackages(const std::vector<package::PackageFile>& packages, const std::filesystem::path& root, database::Database& db) {
        auto tx = transaction::beginTransaction();

        try {
            for (const auto& pkg : packages) {
                log::log(log::LogLevel::Info, "installing " + pkg.metadata.name.value + " " + pkg.metadata.version.value);

                runScript("pre_install", pkg);

                archive::Archive archive{pkg.archivePath};
                auto files = archive::extractPackageContent(archive, root);
                transaction::recordExtractedFiles(tx, files);

                transaction::Transaction::PackageEntry entry;
                entry.pkg = pkg;
                entry.installedFiles = files.value;
                tx.packages.push_back(std::move(entry));
            }

            transaction::commitTransaction(tx, db);
            log::log(log::LogLevel::Info, "transaction committed");

            for (const auto& pkg : packages) {
                runScript("post_install", pkg);
            }
        } catch (...) {
            log::log(log::LogLevel::Error, "transaction failed, rolling back");
            transaction::rollbackTransaction(tx);
            throw;
        }
    }
}
