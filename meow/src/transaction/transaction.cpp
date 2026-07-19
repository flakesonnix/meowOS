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
        std::string txId = generateTransactionId();
        for (const auto& entry : tx.packages) {
            database::registerPackage(db, entry.pkg, entry.installedFiles);
            database::setInstallReason(db, entry.pkg.metadata.name, entry.reason);
            database::recordHistory(db, "install",
                                    entry.pkg.metadata.name,
                                    entry.pkg.metadata.version,
                                    entry.reason, txId);
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
