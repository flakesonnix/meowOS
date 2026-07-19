#include <meow/lock/lockfile.hpp>
#include <meow/error/error.hpp>
#include <meow/format/version.hpp>
#include <toml++/toml.hpp>
#include <sstream>
#include <fstream>

namespace meow::lock {

Lockfile loadLockfile(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw error::MeowError(
            error::ErrorCode::FileNotFound,
            "lockfile not found: " + path.string()
        );
    }

    auto tbl = toml::parse_file(path.string());

    int lfVersion = tbl["lockfile_version"].value_or(0);
    if (lfVersion == 0) lfVersion = tbl["version"].value_or(1);
    format::requireVersion("lockfile", lfVersion, format::CurrentLockfileFormat);

    Lockfile lock;
    lock.version = lfVersion;
    lock.repositoryHash = tbl["repository_hash"].value_or("");

    auto* packagesArr = tbl["package"].as_array();
    if (!packagesArr) return lock;

    for (const auto& elem : *packagesArr) {
        auto* pkgTbl = elem.as_table();
        if (!pkgTbl) continue;

        LockedPackage lp;
        lp.name = types::PackageName{pkgTbl->at("name").value_or("")};
        lp.version = types::PackageVersion{pkgTbl->at("version").value_or("")};

        if (auto* art = (*pkgTbl)["artifact"].as_table()) {
            lp.artifact.filename = art->at("filename").value_or("");
            lp.artifact.url = art->at("url").value_or("");
            lp.artifact.sha256 = art->at("sha256").value_or("");
        }

        lock.packages.push_back(std::move(lp));
    }

    return lock;
}

void saveLockfile(const Lockfile& lock, const std::filesystem::path& path) {
    std::ostringstream ss;

    ss << "lockfile_version = " << lock.version << "\n";
    ss << "repository_hash = \"" << lock.repositoryHash << "\"\n\n";

    for (const auto& lp : lock.packages) {
        ss << "[[package]]\n";
        ss << "name = \"" << lp.name.value << "\"\n";
        ss << "version = \"" << lp.version.value << "\"\n";
        ss << "\n";
        ss << "[package.artifact]\n";
        ss << "filename = \"" << lp.artifact.filename << "\"\n";
        ss << "url = \"" << lp.artifact.url << "\"\n";
        ss << "sha256 = \"" << lp.artifact.sha256 << "\"\n";
        ss << "\n";
    }

    auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream out(path);
    if (!out) {
        throw error::MeowError(
            error::ErrorCode::Internal,
            "cannot write lockfile: " + path.string()
        );
    }
    out << ss.str();
}

const LockedPackage* findLockedPackage(const Lockfile& lock, const types::PackageName& name) {
    for (const auto& lp : lock.packages) {
        if (lp.name.value == name.value) {
            return &lp;
        }
    }
    return nullptr;
}

}
