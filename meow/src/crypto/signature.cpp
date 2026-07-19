#include <meow/crypto/signature.hpp>
#include <meow/error/error.hpp>
#include <meow/log/logger.hpp>
#include <toml++/toml.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <cstdio>
#include <vector>

namespace meow::crypto {

Signature loadSignature(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw error::MeowError(
            error::ErrorCode::FileNotFound,
            "signature file not found: " + path.string()
        );
    }

    auto tbl = toml::parse_file(path.string());
    Signature sig;
    sig.algorithm = tbl["algorithm"].value_or("");
    sig.keyId = tbl["keyId"].value_or("");
    sig.signature = tbl["signature"].value_or("");
    return sig;
}

bool verifyIndex(
    const std::filesystem::path& indexPath,
    const std::filesystem::path& sigPath,
    const std::filesystem::path& keyPath
) {
    if (!std::filesystem::exists(indexPath)) {
        log::log(log::LogLevel::Warning, "index not found");
        return false;
    }
    if (!std::filesystem::exists(sigPath)) {
        log::log(log::LogLevel::Warning, "signature not found");
        return false;
    }
    if (!std::filesystem::exists(keyPath)) {
        log::log(log::LogLevel::Warning, "public key not found");
        return false;
    }

    auto sig = loadSignature(sigPath);

    FILE* f = fopen(keyPath.c_str(), "rb");
    if (!f) return false;
    EVP_PKEY* pkey = PEM_read_PUBKEY(f, nullptr, nullptr, nullptr);
    fclose(f);
    if (!pkey) return false;

    FILE* cf = fopen(indexPath.c_str(), "rb");
    if (!cf) { EVP_PKEY_free(pkey); return false; }
    fseek(cf, 0, SEEK_END);
    long sz = ftell(cf);
    fseek(cf, 0, SEEK_SET);
    std::vector<char> content(sz);
    fread(content.data(), 1, sz, cf);
    fclose(cf);

    int pad = 0;
    if (!sig.signature.empty() && sig.signature.back() == '=') pad++;
    if (sig.signature.size() >= 2 && sig.signature[sig.signature.size() - 2] == '=') pad++;

    std::vector<unsigned char> sigBytes(sig.signature.size() * 3 / 4 + 1);
    int sigLen = EVP_DecodeBlock(
        sigBytes.data(),
        reinterpret_cast<const unsigned char*>(sig.signature.data()),
        static_cast<int>(sig.signature.size())
    );
    if (sigLen < 0) { EVP_PKEY_free(pkey); return false; }
    sigLen -= pad;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    bool ok = false;
    if (ctx) {
        if (EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey) == 1) {
            int rc = EVP_DigestVerify(
                ctx,
                sigBytes.data(), sigLen,
                reinterpret_cast<unsigned char*>(content.data()), content.size()
            );
            ok = (rc == 1);
        }
        EVP_MD_CTX_free(ctx);
    }

    EVP_PKEY_free(pkey);
    return ok;
}

}
