// Boundary tests for the v0.7 signed package index (groundwork only).
// Exercises the placeholder API in meow/repository/package_index.hpp WITHOUT
// wiring it into production paths. No resolver/SAT coverage.

#include <meow/repository/package_index.hpp>
#include <meow/error/error.hpp>

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace meow::repository;

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%d)\n", #cond, __LINE__); ++failures; } \
} while (0)

static fs::path dir() {
    auto d = fs::temp_directory_path() / "meow-test-pkgidx";
    fs::create_directories(d);
    return d;
}

// An unsigned package index is parsed and accepted in compatibility mode
// (requirePackageIndex = false). The new field (artifact_hash) is read.
static void test_unsigned_index_accepted() {
    auto d = dir();
    auto p = d / "packages.toml";
    std::ofstream out(p);
    out << "format_version = 1\n";
    out << "generated = \"2026-07-20T00:00:00Z\"\n\n";
    out << "[[package]]\n";
    out << "name = \"hello\"\n";
    out << "version = \"1.1.0\"\n";
    out << "manifest_hash = \"sha256:aaaa\"\n";
    out << "artifact_hash = \"sha256:bbbb\"\n";
    out << "size = 131072\n";
    out << "dependencies = [\"glibc>=2.38\"]\n";
    out.close();

    auto idx = parsePackageIndex(p);
    CHECK(idx.formatVersion == 1);
    CHECK(idx.packages.size() == 1);
    CHECK(idx.packages[0].name == "hello");
    CHECK(idx.packages[0].artifactHash == "sha256:bbbb");
    CHECK(idx.packages[0].size == 131072);
    CHECK(idx.packages[0].dependencies.size() == 1);

    // Compatibility mode: unsigned index acceptable.
    CHECK(acceptUnsignedPackageIndex(false) == true);
    // Strict mode (future): unsigned index NOT acceptable.
    CHECK(acceptUnsignedPackageIndex(true) == false);

    fs::remove(p);
}

// verifyPackageIndex rejects a missing index or missing signature with
// InvalidSignature (design contract for v0.7). No trusted key is required
// to detect the missing-file case.
static void test_invalid_signature_handling() {
    auto d = dir();
    auto p = d / "packages.toml";
    {
        std::ofstream out(p);
        out << "format_version = 1\n";
    }
    // Index present but no .sig -> should throw InvalidSignature.
    bool threw = false;
    try {
        verifyPackageIndex(d, "default");
    } catch (const meow::error::MeowError& e) {
        threw = true;
        CHECK(e.code == meow::error::ErrorCode::InvalidSignature);
    }
    CHECK(threw);

    // Neither index nor sig present -> also InvalidSignature.
    fs::remove(p);
    threw = false;
    try {
        verifyPackageIndex(d, "default");
    } catch (const meow::error::MeowError& e) {
        threw = true;
        CHECK(e.code == meow::error::ErrorCode::InvalidSignature);
    }
    CHECK(threw);
}

int main() {
    test_unsigned_index_accepted();
    test_invalid_signature_handling();

    if (failures == 0) {
        std::printf("all package-index boundary tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
}
