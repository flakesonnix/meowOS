#include <meow/verify/verifier.hpp>
#include <meow/database/database.hpp>
#include <meow/download/downloader.hpp>
#include <meow/log/logger.hpp>
#include <filesystem>

namespace meow::verify {

    VerificationResult verifyPackage(
        database::Database& db,
        const types::PackageName& name
    ) {
        VerificationResult result;

        auto entries = database::listPackageFileEntries(db, name);

        for (const auto& entry : entries) {
            if (!std::filesystem::exists(entry.path)) {
                result.missing.push_back(entry.path);
                continue;
            }

            if (!entry.sha256.empty()) {
                auto currentHash = download::computeFileHash(entry.path);
                if (currentHash != entry.sha256) {
                    result.modified.push_back(entry.path);
                }
            }
        }

        return result;
    }

    VerificationResult verifyAll(database::Database& db) {
        VerificationResult combined;
        auto packages = database::listInstalled(db);

        for (const auto& name : packages) {
            log::log(log::LogLevel::Info, "checking " + name.value);
            auto result = verifyPackage(db, name);
            combined.missing.insert(combined.missing.end(),
                result.missing.begin(), result.missing.end());
            combined.modified.insert(combined.modified.end(),
                result.modified.begin(), result.modified.end());
        }

        return combined;
    }

}
