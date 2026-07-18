#ifndef MEOWOS_VERIFIER_H
#define MEOWOS_VERIFIER_H

#include <filesystem>
#include <vector>

#include <meow/database/database.hpp>
#include <meow/types/types.hpp>

namespace meow::verify {

struct VerificationResult {
    std::vector<std::filesystem::path> missing;
    std::vector<std::filesystem::path> modified;
};

VerificationResult verifyPackage(
    database::Database& db,
    const types::PackageName& name
);

VerificationResult verifyAll(
    database::Database& db
);

}

#endif
