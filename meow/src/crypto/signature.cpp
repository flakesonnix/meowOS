#include <meow/crypto/signature.hpp>
#include <meow/error/error.hpp>
#include <meow/log/logger.hpp>
#include <toml++/toml.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <cstdio>
#include <vector>
#include <sstream>

namespace meow::crypto {

Signature loadSignature(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw error::MeowError(
            error::ErrorCode::FileNotFound,
            "signature file not found: " + path.string()
        );
    }

    // A corrupt/malformed .sig must fail closed as InvalidSignature rather
    // than propagating a raw parser exception that would bypass the signature
    // policy in verifyRepoSig (audit item 2 — corrupt .sig bypass).
    toml::table tbl;
    try {
        tbl = toml::parse_file(path.string());
    } catch (const std::exception& e) {
        throw error::MeowError(
            error::ErrorCode::InvalidSignature,
            "cannot parse signature file " + path.string() + ": " +
                std::string(e.what()));
    }
    Signature sig;
    sig.algorithm = tbl["algorithm"].value_or("");
    sig.keyId = tbl["keyId"].value_or("");
    sig.signature = tbl["signature"].value_or("");
    return sig;
}

bool verifyFile(
    const std::filesystem::path& filePath,
    const std::filesystem::path& sigPath,
    const std::filesystem::path& keyPath
) {
    if (!std::filesystem::exists(filePath)) return false;
    if (!std::filesystem::exists(sigPath)) return false;
    if (!std::filesystem::exists(keyPath)) return false;

    auto sig = loadSignature(sigPath);

    FILE* f = fopen(keyPath.c_str(), "rb");
    if (!f) return false;
    EVP_PKEY* pkey = PEM_read_PUBKEY(f, nullptr, nullptr, nullptr);
    fclose(f);
    if (!pkey) return false;

    FILE* cf = fopen(filePath.c_str(), "rb");
    if (!cf) { EVP_PKEY_free(pkey); return false; }
    fseek(cf, 0, SEEK_END);
    long sz = ftell(cf);
    fseek(cf, 0, SEEK_SET);
    std::vector<char> content(sz);
    (void)fread(content.data(), 1, sz, cf);
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

void saveSignature(const Signature& sig, const std::filesystem::path& path) {
    std::ostringstream ss;
    ss << "algorithm = \"" << sig.algorithm << "\"\n"
       << "keyId = \"" << sig.keyId << "\"\n"
       << "signature = \"" << sig.signature << "\"\n";
    auto content = ss.str();
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        throw error::MeowError(error::ErrorCode::FileNotFound,
            "cannot write signature: " + path.string());
    }
    fwrite(content.data(), 1, content.size(), f);
    fclose(f);
}

void signFile(
    const std::filesystem::path& filePath,
    const std::filesystem::path& keyPath,
    const std::filesystem::path& sigPath,
    const std::string& keyId
) {
    FILE* f = fopen(keyPath.c_str(), "rb");
    if (!f) {
        throw error::MeowError(error::ErrorCode::FileNotFound,
            "key not found: " + keyPath.string());
    }
    EVP_PKEY* pkey = PEM_read_PrivateKey(f, nullptr, nullptr, nullptr);
    fclose(f);
    if (!pkey) {
        throw error::MeowError(error::ErrorCode::InvalidSignature,
            "failed to read private key");
    }

    FILE* cf = fopen(filePath.c_str(), "rb");
    if (!cf) { EVP_PKEY_free(pkey); return; }
    fseek(cf, 0, SEEK_END);
    long sz = ftell(cf);
    fseek(cf, 0, SEEK_SET);
    std::vector<unsigned char> content(sz);
    (void)fread(content.data(), 1, sz, cf);
    fclose(cf);

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) { EVP_PKEY_free(pkey); return; }

    size_t sigLen = EVP_PKEY_size(pkey);
    std::vector<unsigned char> rawSig(sigLen);

    int rc = EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, pkey);
    if (rc == 1) {
        rc = EVP_DigestSign(ctx, rawSig.data(), &sigLen,
                           content.data(), content.size());
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);

    if (rc != 1) {
        throw error::MeowError(error::ErrorCode::InvalidSignature,
            "signing failed");
    }

    size_t b64Len = ((sigLen + 2) / 3) * 4 + 1;
    std::vector<unsigned char> b64(b64Len);
    int encodeLen = EVP_EncodeBlock(b64.data(), rawSig.data(),
                                     static_cast<int>(sigLen));
    if (encodeLen < 0) {
        throw error::MeowError(error::ErrorCode::InvalidSignature,
            "base64 encode failed");
    }

    Signature sig;
    sig.algorithm = "ed25519";
    sig.keyId = keyId;
    sig.signature = reinterpret_cast<const char*>(b64.data());
    saveSignature(sig, sigPath);
}

}
