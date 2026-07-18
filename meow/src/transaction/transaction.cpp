#include <meow/transaction/transaction.hpp>
#include <meow/error/error.hpp>

namespace meow::transaction {
    Transaction beginTransaction() {
        return Transaction{};
    }

    void recordExtractedFiles(Transaction& tx, const types::FileList& files) {
        for (const auto& f : files.value) {
            tx.createdFiles.push_back(f);
        }
    }

    void commitTransaction(Transaction& tx, database::Database& db) {
        for (const auto& entry : tx.packages) {
            database::registerPackage(db, entry.pkg, entry.installedFiles);
        }
        tx.committed = true;
    }

    void rollbackTransaction(const Transaction& tx) {
        for (auto it = tx.createdFiles.rbegin(); it != tx.createdFiles.rend(); ++it) {
            std::error_code ec;
            std::filesystem::remove(*it, ec);
        }
    }
}
