#ifndef MEOWOS_PACKAGE_INDEX_H
#define MEOWOS_PACKAGE_INDEX_H

#include <filesystem>
#include <string>
#include <vector>

#include <meow/error/error.hpp>
#include <meow/crypto/signature.hpp>
#include <meow/crypto/keystore.hpp>
#include <toml++/toml.hpp>

namespace meow::repository {

// v0.7 groundwork — placeholder abstraction for the signed package index.
//
// This header defines the *interface boundary* only. It is intentionally NOT
// wired into any production path yet (no backend calls it, the
// requirePackageIndex flag defaults to false). It exists so the v0.7 migration
// has a stable entry point and so the verification contract can be tested in
// isolation.
//
// Design (see docs/package-index-signing-implementation-plan.md):
//   packages.toml      — canonical TOML mapping every package version to its
//                          manifest_hash, artifact_hash, size, dependencies
//   packages.toml.sig — Ed25519 detached signature (same key as
//                          repository.toml.sig)
// The client verifies the index signature, then checks each loaded manifest /
// artifact hash against the signed entry. An attacker-substituted manifest or
// artifact fails the hash comparison (closes HIGH #1).

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

// Parse `packages.toml` (unsigned) into a PackageIndex. Throws MeowError on
// malformed input. Does NOT verify the signature.
inline PackageIndex parsePackageIndex(const std::filesystem::path& indexPath) {
    if (!std::filesystem::exists(indexPath)) {
        throw error::MeowError(error::ErrorCode::FileNotFound,
            "package index not found: " + indexPath.string());
    }
    auto tbl = toml::parse_file(indexPath.string());
    PackageIndex idx;
    idx.formatVersion = tbl["format_version"].value_or(1);
    idx.generated = tbl["generated"].value_or("");

    auto* arr = tbl["package"].as_array();
    if (!arr) return idx;
    for (const auto& elem : *arr) {
        auto* t = elem.as_table();
        if (!t) continue;
        PackageIndexEntry e;
        e.name = (*t)["name"].value_or("");
        e.version = (*t)["version"].value_or("");
        e.manifestHash = (*t)["manifest_hash"].value_or("");
        e.artifactHash = (*t)["artifact_hash"].value_or("");
        e.size = (*t)["size"].value_or(0);
        if (auto* deps = (*t)["dependencies"].as_array()) {
            for (const auto& d : *deps) {
                if (auto v = d.value<std::string>()) e.dependencies.push_back(*v);
            }
        }
        idx.packages.push_back(std::move(e));
    }
    return idx;
}

// Verify the detached signature over `packages.toml` against a trusted key.
// Throws MeowError (InvalidSignature / TrustedKeyNotFound) on failure, returns
// normally on success. Pure verification — no side effects.
inline void verifyPackageIndex(
    const std::filesystem::path& indexDir,
    const std::string& keyId
) {
    auto index = indexDir / "packages.toml";
    auto sig = indexDir / "packages.toml.sig";
    if (!std::filesystem::exists(index) || !std::filesystem::exists(sig)) {
        throw error::MeowError(error::ErrorCode::InvalidSignature,
            "package index or its signature is missing");
    }
    auto key = crypto::loadTrustedKey(keyId);
    if (!crypto::verifyFile(index, sig, key.path)) {
        throw error::MeowError(error::ErrorCode::InvalidSignature,
            "package index signature invalid");
    }
}

// Compatibility gate. Returns true when an unsigned / missing index is
// acceptable (current default behavior). When requirePackageIndex is set, a
// missing or invalid index is a hard error instead. Mirrors the existing
// requireRepositorySignature contract.
inline bool acceptUnsignedPackageIndex(bool requirePackageIndex) {
    return !requirePackageIndex;
}

}

#endif // MEOWOS_PACKAGE_INDEX_H
