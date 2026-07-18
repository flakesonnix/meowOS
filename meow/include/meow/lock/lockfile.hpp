#ifndef MEOWOS_LOCKFILE_H
#define MEOWOS_LOCKFILE_H

#include <filesystem>
#include <string>
#include <vector>

#include <meow/types/types.hpp>

namespace meow::lock {

struct LockedPackage {
    types::PackageName name;
    types::PackageVersion version;
    types::PackageArtifact artifact;
};

struct Lockfile {
    int version = 1;
    std::string repositoryHash;
    std::vector<LockedPackage> packages;
};

Lockfile loadLockfile(const std::filesystem::path& path);
void saveLockfile(const Lockfile& lock, const std::filesystem::path& path);
const LockedPackage* findLockedPackage(const Lockfile& lock, const types::PackageName& name);

}

#endif
