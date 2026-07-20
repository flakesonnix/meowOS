#ifndef MEOWOS_PACKAGE_INDEX_H
#define MEOWOS_PACKAGE_INDEX_H

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <meow/error/error.hpp>
#include <meow/crypto/signature.hpp>
#include <meow/crypto/keystore.hpp>

namespace meow::repository {

// v0.7 signed package index.
//
// Design (see docs/package-index-signing-implementation-plan.md):
//   packages.toml      — canonical TOML mapping every package version to its
//                          manifest_hash, artifact_hash, size, dependencies
//   packages.toml.sig — Ed25519 detached signature (same key as
//                          repository.toml.sig)
// The client verifies the index signature, then checks each loaded manifest /
// artifact hash against the signed entry. An attacker-substituted manifest or
// artifact fails the hash comparison (closes HIGH #1).
//
// This header holds only the lightweight data types and the crypto-only
// canonical hash so it can be included by the public repository.hpp without
// pulling in toml++. The TOML parsing / signature-verification entry points are
// declared here and implemented in package_index.cpp (part of meow_core).

struct PackageIndexEntry {
    std::string name;
    std::string version;
    std::string manifestHash;   // sha256 over canonical package.toml + version toml
    std::string artifactHash;    // sha256 over the .pkg.tar.zst
    uint64_t size = 0;
    std::vector<std::string> dependencies;
};

struct PackageIndex {
    int formatVersion = 1;
    std::string generated;
    std::vector<PackageIndexEntry> packages;
};

// Canonical manifest hash for the signed index. Defined as
//   sha256( raw bytes of package.toml  ||  raw bytes of versions/<ver>.toml )
// The repo-builder and every client compute this identically so the signed
// index entry can be recomputed and compared byte-for-byte. Both files are
// read in binary mode with no normalization, so the digest is stable across
// tools as long as the on-disk bytes match. Crypto-only (no toml++), so it is
// safe to keep inline in this widely-included header.
inline std::string computeManifestHash(const std::filesystem::path& pkgMetaPath,
                                       const std::filesystem::path& versionPath) {
    auto readBytes = [](const std::filesystem::path& p) -> std::string {
        std::ifstream f(p, std::ios::binary);
        if (!f) {
            throw error::MeowError(error::ErrorCode::FileNotFound,
                "cannot read manifest for hashing: " + p.string());
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };
    std::string combined = readBytes(pkgMetaPath);
    combined += readBytes(versionPath);
    return crypto::computeSha256Bytes(combined);
}

// Parse `packages.toml` (unsigned) into a PackageIndex. Throws MeowError on
// malformed input. Does NOT verify the signature.
PackageIndex parsePackageIndex(const std::filesystem::path& indexPath);

// Verify the detached signature over `packages.toml` against a trusted key.
// Throws MeowError (InvalidSignature / TrustedKeyNotFound) on failure, returns
// normally on success. Pure verification — no side effects.
void verifyPackageIndex(const std::filesystem::path& indexDir,
                        const std::string& keyId);

// Compatibility gate. Returns true when an unsigned / missing index is
// acceptable (current default behavior). When requirePackageIndex is set, a
// missing or invalid index is a hard error instead. Mirrors the existing
// requireRepositorySignature contract.
inline bool acceptUnsignedPackageIndex(bool requirePackageIndex) {
    return !requirePackageIndex;
}

}

#endif // MEOWOS_PACKAGE_INDEX_H
