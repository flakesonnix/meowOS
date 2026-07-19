#ifndef MEOWOS_SIGNATURE_H
#define MEOWOS_SIGNATURE_H

#include <filesystem>
#include <string>

namespace meow::crypto {

struct Signature {
    std::string algorithm;
    std::string keyId;
    std::string signature;
};

Signature loadSignature(const std::filesystem::path& path);

bool verifyIndex(
    const std::filesystem::path& indexPath,
    const std::filesystem::path& sigPath,
    const std::filesystem::path& keyPath
);

}

#endif
