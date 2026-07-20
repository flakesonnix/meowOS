#include <meow/lock/install_lock.hpp>
#include <meow/error/error.hpp>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace meow::lock {

InstallLock::InstallLock(const std::filesystem::path& path) : fd_(-1), path_(path) {
    std::error_code ec;
    if (auto parent = path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }

    fd_ = open(path.string().c_str(), O_RDWR | O_CREAT, 0600);
    if (fd_ < 0) {
        throw error::MeowError(
            error::ErrorCode::Internal,
            "cannot open install lock: " + path.string());
    }

    // Non-blocking: fail cleanly instead of waiting. flock is released by the
    // kernel when the file descriptor is closed (on destruction / process exit).
    if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd_);
        fd_ = -1;
        throw error::MeowError(
            error::ErrorCode::AlreadyLocked,
            "another meow install/upgrade/remove is already running\n"
            "  lock: " + path.string());
    }
}

InstallLock::~InstallLock() {
    if (fd_ >= 0) {
        flock(fd_, LOCK_UN);
        ::close(fd_);
        fd_ = -1;
    }
}

std::filesystem::path defaultInstallLockPath(const std::filesystem::path& dbPath) {
    if (dbPath.empty()) {
        const char* home = std::getenv("HOME");
        if (!home) {
            throw error::MeowError(error::ErrorCode::Internal, "HOME not set");
        }
        return std::filesystem::path(home) / ".cache" / "meow" / "install.lock";
    }
    return dbPath.parent_path() / "install.lock";
}

}
