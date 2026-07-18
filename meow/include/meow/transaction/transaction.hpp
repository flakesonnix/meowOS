#ifndef MEOWOS_TRANSACTION_H
#define MEOWOS_TRANSACTION_H

#include <filesystem>
#include <vector>

#include <meow/types/types.hpp>
#include <meow/package/package.hpp>
#include <meow/database/database.hpp>

namespace meow::transaction {
    struct Transaction {
        std::vector<package::PackageFile> packages;
        std::vector<std::filesystem::path> createdFiles;
        bool committed = false;
    };

    Transaction beginTransaction();
    void recordExtractedFiles(Transaction& tx, const types::FileList& files);
    void commitTransaction(Transaction& tx, database::Database& db);
    void rollbackTransaction(const Transaction& tx);
}

#endif //MEOWOS_TRANSACTION_H
