#ifndef MEOWOS_CRYPTO_H
#define MEOWOS_CRYPTO_H

#include <filesystem>
#include <string>

namespace meow::crypto {

struct Signature {
    std::string algorithm;
    std::string keyId;
    std::string signature;
};

Signature loadSignature(const std::filesystem::path& path);

bool verifyFile(
    const std::filesystem::path& filePath,
    const std::filesystem::path& sigPath,
    const std::filesystem::path& keyPath
);

}

#endif
