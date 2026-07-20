#include <meow/repository/package_index.hpp>

#include <toml++/toml.hpp>

namespace meow::repository {

PackageIndex parsePackageIndex(const std::filesystem::path& indexPath) {
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

void verifyPackageIndex(const std::filesystem::path& indexDir,
                        const std::string& keyId) {
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

}
