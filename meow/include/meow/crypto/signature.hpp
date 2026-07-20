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
void saveSignature(const Signature& sig, const std::filesystem::path& path);

bool verifyFile(
    const std::filesystem::path& filePath,
    const std::filesystem::path& sigPath,
    const std::filesystem::path& keyPath
);

// Compute the hex-encoded SHA-256 of a file using OpenSSL (in-process, no
// shell). Throws MeowError on read failure.
std::string computeSha256(const std::filesystem::path& file);

// Compute the hex-encoded SHA-256 of an in-memory byte buffer using OpenSSL.
// Used for canonical hashes over concatenated metadata (e.g. the signed
// package index manifest hash). Throws MeowError on OpenSSL failure.
std::string computeSha256Bytes(const std::string& bytes);

void signFile(
    const std::filesystem::path& filePath,
    const std::filesystem::path& keyPath,
    const std::filesystem::path& sigPath,
    const std::string& keyId = "default"
);

}

#endif
