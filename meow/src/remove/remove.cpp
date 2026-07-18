#include <meow/remove/remove.hpp>
#include <meow/transaction/transaction.hpp>
#include <meow/error/error.hpp>
#include <iostream>

namespace meow::remove {
    void removePackage(const types::PackageName& name, database::Database& db) {
        if (!database::isInstalled(db, name)) {
            throw error::MeowError(error::ErrorCode::PackageNotFound, "package not installed: " + name.value);
        }

        auto files = database::listPackageFiles(db, name);

        auto tx = transaction::beginTransaction();
        try {
            for (const auto& f : files) {
                std::error_code ec;
                std::filesystem::remove(f, ec);
                if (ec) {
                    std::cerr << "  warning: could not remove " << f.string() << ": " << ec.message() << "\n";
                } else {
                    std::cout << "  removed " << f.string() << "\n";
                }
            }

            database::removePackageRecord(db, name);
            tx.committed = true;
            std::cout << "  removed " << name.value << " from database\n";
        } catch (...) {
            std::cerr << "  remove failed, transaction not committed\n";
            throw;
        }
    }
}
