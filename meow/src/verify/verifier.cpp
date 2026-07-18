#include <meow/verify/verifier.hpp>
#include <meow/download/downloader.hpp>
#include <iostream>

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

VerificationResult verifyAll(
    database::Database& db
) {
    VerificationResult combined;
    auto packages = database::listInstalled(db);

    for (const auto& pkg : packages) {
        std::cout << "  " << pkg.value << "\n";
        auto pkgResult = verifyPackage(db, pkg);
        for (const auto& f : pkgResult.missing) {
            std::cout << "    \x1b[31m\u2717 " << f.string() << " (missing)\x1b[0m\n";
            combined.missing.push_back(f);
        }
        for (const auto& f : pkgResult.modified) {
            std::cout << "    \x1b[33m\u2717 " << f.string() << " (modified)\x1b[0m\n";
            combined.modified.push_back(f);
        }
    }

    return combined;
}

}
