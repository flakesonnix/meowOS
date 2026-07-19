#include <meow/remove/remove.hpp>
#include <meow/transaction/transaction.hpp>
#include <meow/error/error.hpp>
#include <meow/log/logger.hpp>

namespace meow::remove {
    void removePackage(const types::PackageName& name, database::Database& db) {
        if (!database::isInstalled(db, name)) {
            throw error::MeowError(error::ErrorCode::PackageNotFound, "package not installed: " + name.value);
        }

        auto dependents = database::requiredBy(db, name);
        if (!dependents.empty()) {
            std::string msg = "cannot remove " + name.value + ": required by";
            for (const auto& d : dependents) {
                msg += " " + d.value;
            }
            throw error::MeowError(error::ErrorCode::DependencyNotFound, msg);
        }

        auto files = database::listPackageFiles(db, name);
        auto removedVer = database::installedVersion(db, name);
        auto removedReason = database::installReason(db, name).value_or(database::InstallReason::Dependency);

        auto tx = transaction::beginTransaction();
        try {
            for (const auto& f : files) {
                std::error_code ec;
                std::filesystem::remove(f, ec);
                if (ec) {
                    log::log(log::LogLevel::Warning, "could not remove " + f.string() + ": " + ec.message());
                }
            }

            database::removePackageRecord(db, name);
            if (removedVer) {
                database::recordHistory(db, "remove", name, *removedVer, removedReason, "");
            }
            tx.committed = true;
            log::log(log::LogLevel::Info, "removed " + name.value + " from database");
        } catch (...) {
            log::log(log::LogLevel::Error, "remove failed, transaction not committed");
            throw;
        }
    }
}
