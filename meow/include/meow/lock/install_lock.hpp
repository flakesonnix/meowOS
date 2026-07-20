#ifndef MEOWOS_INSTALL_LOCK_H
#define MEOWOS_INSTALL_LOCK_H

#include <filesystem>
#include <string>

namespace meow::lock {

// Advisory, process-level mutex that serializes mutating operations
// (install/upgrade/remove) so two concurrent `meow` invocations cannot race
// on the filesystem and the database. Backed by flock(2) on a lock file, so
// it is safe across processes and released automatically when the holder
// exits (including on crash).
//
// The lock must be acquired for the whole transaction + database commit
// window. A second holder fails cleanly with error::AlreadyLocked rather
// than blocking.
class InstallLock {
public:
    // Acquire the lock at `path`. Throws error::AlreadyLocked if another
    // process already holds it.
    explicit InstallLock(const std::filesystem::path& path);
    InstallLock(const InstallLock&) = delete;
    InstallLock& operator=(const InstallLock&) = delete;
    InstallLock(InstallLock&&) = delete;
    InstallLock& operator=(InstallLock&&) = delete;
    ~InstallLock();

private:
    int fd_;
    std::filesystem::path path_;
};

// Default lock file location (next to the database). Kept in one place so
// install/upgrade/remove share the same mutex.
std::filesystem::path defaultInstallLockPath(const std::filesystem::path& dbPath);

}

#endif
