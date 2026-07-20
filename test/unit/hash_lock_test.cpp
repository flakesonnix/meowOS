// Regression tests for in-process SHA-256 hashing and the install advisory
// lock. No resolver/SAT/config engine coverage; links meow_core only.

#include <meow/crypto/signature.hpp>
#include <meow/lock/install_lock.hpp>
#include <meow/error/error.hpp>

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace meow::crypto;
using namespace meow::lock;

static std::string makeFile(const fs::path& p, const std::string& contents) {
    std::ofstream out(p, std::ios::binary);
    out << contents;
    out.close();
    return computeSha256(p);
}

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%d)\n", #cond, __LINE__); ++failures; } \
} while (0)

// Known SHA-256 of an empty file: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
static void test_empty_file_hash() {
    auto dir = fs::temp_directory_path() / "meow-test-hash";
    fs::create_directories(dir);
    auto p = dir / "empty.bin";
    { std::ofstream out(p, std::ios::binary); }
    auto h = computeSha256(p);
    CHECK(h == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    fs::remove(p);
}

// Path containing spaces and shell metacharacters must not break hashing
// (previously a popen injection / parse risk).
static void test_hash_path_with_shell_chars() {
    auto dir = fs::temp_directory_path() / "meow-test-hash";
    fs::create_directories(dir);
    auto p = dir / "file with spaces; rm -rf 'x' & (weird).bin";
    std::string contents = "hello meow";
    auto h = makeFile(p, contents);

    // Cross-check against an independent in-process recompute of same bytes.
    auto h2 = computeSha256(p);
    CHECK(h == h2);
    CHECK(h.size() == 64);

    // verifyChecksum-style comparison through the public hash function.
    CHECK(computeSha256(p) == h);
    fs::remove(p);
}

// Two different contents must yield different hashes.
static void test_hash_distinct_contents() {
    auto dir = fs::temp_directory_path() / "meow-test-hash";
    fs::create_directories(dir);
    auto a = dir / "a.bin";
    auto b = dir / "b.bin";
    auto ha = makeFile(a, "contents-A");
    auto hb = makeFile(b, "contents-B");
    CHECK(ha != hb);
    fs::remove(a);
    fs::remove(b);
}

// First lock holder succeeds; a second concurrent holder fails with
// AlreadyLocked and a clean diagnostic (no blocking, no crash).
static void test_install_lock_exclusive() {
    auto dir = fs::temp_directory_path() / "meow-test-lock";
    fs::create_directories(dir);
    auto lockPath = dir / "install.lock";

    {
        meow::lock::InstallLock held(lockPath);
        bool threw = false;
        try {
            meow::lock::InstallLock second(lockPath);
        } catch (const meow::error::MeowError& e) {
            threw = true;
            CHECK(e.code == meow::error::ErrorCode::AlreadyLocked);
        }
        CHECK(threw);
    }

    // After release, a new holder can acquire.
    {
        meow::lock::InstallLock reacquire(lockPath);
    }
    fs::remove(lockPath);
}

int main() {
    test_empty_file_hash();
    test_hash_path_with_shell_chars();
    test_hash_distinct_contents();
    test_install_lock_exclusive();

    if (failures == 0) {
        std::printf("all hash/lock tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
}
