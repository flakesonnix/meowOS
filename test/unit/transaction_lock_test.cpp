// Regression tests for install-lock coverage across mutation paths and
// rollback cleanup. Exercises meow::lock::InstallLock semantics that the
// install/remove/upgrade/update/repair paths now depend on. Links meow_core
// only; no resolver/SAT coverage.

#include <meow/lock/install_lock.hpp>
#include <meow/error/error.hpp>

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace meow::lock;

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%d)\n", #cond, __LINE__); ++failures; } \
} while (0)

static fs::path lockPath() {
    auto p = fs::temp_directory_path() / "meow-test-txlock" / "install.lock";
    return p;
}

// Concurrent install blocked: a second holder while the first is active
// fails with AlreadyLocked rather than blocking or crashing.
static void test_concurrent_install_blocked() {
    auto p = lockPath();
    {
        InstallLock held(p);
        bool threw = false;
        try {
            InstallLock second(p);
        } catch (const meow::error::MeowError& e) {
            threw = true;
            CHECK(e.code == meow::error::ErrorCode::AlreadyLocked);
        }
        CHECK(threw);
    }
    fs::remove(p);
}

// Concurrent remove/update blocked: any two mutation-style holders contend
// for the same lock (mirrors remove/upgrade/update/repair all sharing it).
static void test_concurrent_remove_update_blocked() {
    auto p = lockPath();
    InstallLock removeLock(p);  // stands in for `remove`
    bool threw = false;
    try {
        InstallLock updateLock(p);  // stands in for `update`/`upgrade`/`repair`
    } catch (const meow::error::MeowError& e) {
        threw = true;
        CHECK(e.code == meow::error::ErrorCode::AlreadyLocked);
    }
    CHECK(threw);
    fs::remove(p);
}

// Lock released after failure: when the holder scope exits via exception,
// a subsequent operation can reacquire the lock.
static void test_lock_released_after_failure() {
    auto p = lockPath();
    bool innerThrew = false;
    try {
        InstallLock held(p);
        throw std::runtime_error("simulated txn failure");
    } catch (const std::runtime_error&) {
        innerThrew = true;
    }
    CHECK(innerThrew);

    // Reacquire must succeed now that the failed scope released the lock.
    bool reacquired = false;
    try {
        InstallLock again(p);
        reacquired = true;
    } catch (const meow::error::MeowError&) {
        reacquired = false;
    }
    CHECK(reacquired);
    fs::remove(p);
}

// Successful operation can reacquire: lock released on normal completion.
static void test_success_reacquires() {
    auto p = lockPath();
    {
        InstallLock first(p);
    }
    {
        InstallLock second(p);  // must succeed
    }
    fs::remove(p);
}

int main() {
    test_concurrent_install_blocked();
    test_concurrent_remove_update_blocked();
    test_lock_released_after_failure();
    test_success_reacquires();

    if (failures == 0) {
        std::printf("all transaction-lock tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
}
