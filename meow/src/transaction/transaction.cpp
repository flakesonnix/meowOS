#include <meow/transaction/transaction.hpp>
#include <meow/error/error.hpp>
#include <meow/database/database.hpp>

#include <chrono>
#include <random>
#include <sstream>

namespace meow::transaction {
    namespace {
        // Best-effort UUID v4 (no external dependency). Uniqueness across
        // installs is all we need to group one transaction's history rows.
        std::string generateTransactionId() {
            std::random_device rd;
            std::mt19937_64 gen(rd() ^
                std::chrono::system_clock::now().time_since_epoch().count());
            std::uniform_int_distribution<uint64_t> dist;
            auto h = [](uint64_t v) {
                std::ostringstream os;
                os << std::hex << v;
                return os.str();
            };
            std::string a = h(dist(gen)), b = h(dist(gen));
            return a.substr(0, 8) + "-" + a.substr(8, 4) + "-" +
                   b.substr(0, 4) + "-" + b.substr(4, 4) + "-" + b.substr(8, 12);
        }
    }  // namespace

    Transaction beginTransaction() {
        return Transaction{};
    }

    void recordExtractedFiles(Transaction& tx, const types::FileList& files) {
        for (const auto& f : files.value) {
            tx.createdFiles.push_back(f);
        }
    }

    void commitTransaction(Transaction& tx, database::Database& db) {
        // Wrap all DB writes in an SQLite transaction so that a partial failure
        // (e.g. the N-th package's registerPackage throws) rolls back every
        // earlier write, keeping DB and filesystem consistent.
        database::execRaw(db, "BEGIN IMMEDIATE TRANSACTION");
        bool began = true;
        try {
            std::string txId = generateTransactionId();
            for (const auto& entry : tx.packages) {
                if (entry.clearExistingFiles) {
                    database::removePackageFiles(db, entry.pkg.metadata.name);
                }
                database::registerPackage(db, entry.pkg, entry.installedFiles);
                database::setInstallReason(db, entry.pkg.metadata.name, entry.reason);
                database::recordHistory(db, "install",
                                        entry.pkg.metadata.name,
                                        entry.pkg.metadata.version,
                                        entry.reason, txId);
            }
            database::execRaw(db, "COMMIT");
            began = false;
            tx.committed = true;
        } catch (...) {
            if (began) {
                try { database::execRaw(db, "ROLLBACK"); } catch (...) {}
            }
            throw;
        }
    }

    void rollbackTransaction(const Transaction& tx) {
        for (auto it = tx.createdFiles.rbegin(); it != tx.createdFiles.rend(); ++it) {
            std::error_code ec;
            std::filesystem::remove(*it, ec);
            // Best-effort cleanup of now-empty parent directories left behind
            // by the rolled-back extraction, so a failed transaction does not
            // leave dangling empty directories in the install root.
            auto parent = it->parent_path();
            for (int depth = 0; depth < 16 && !parent.empty(); ++depth) {
                std::error_code pec;
                if (!std::filesystem::is_empty(parent, pec) || pec) break;
                std::filesystem::remove(parent, pec);
                if (pec) break;
                parent = parent.parent_path();
            }
        }
    }
}
