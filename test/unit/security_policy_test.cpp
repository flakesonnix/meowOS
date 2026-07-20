// Unit tests for the repository security policy (require_repository_signature).
//
// Builds a minimal *unsigned* filesystem repository in a temp directory and
// verifies:
//   - default policy: unsigned repo loads (warn-and-continue, legacy behavior)
//   - require policy : unsigned repo is rejected with InvalidSignature
//
// Regression test for v0.5 hardening audit item 2 (require-signature mode).
// Uses a temp HOME so the real user cache/keys are never touched.

#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <meow/error/error.hpp>
#include <meow/repository/repository.hpp>
#include <meow/repository/security_policy.hpp>

namespace fs = std::filesystem;
using namespace meow;

namespace {

int failures = 0;

void expectPass(const std::string& what, bool ok) {
    std::cout << (ok ? "  PASS: " : "  FAIL: ") << what << "\n";
    if (!ok) ++failures;
}

void writeFile(const fs::path& p, const std::string& contents) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << contents;
}

// Build a valid but UNSIGNED filesystem repository (no repository.toml.sig).
void buildUnsignedRepo(const fs::path& root) {
    writeFile(root / "repository.toml",
              "format_version = 1\n"
              "name = \"testrepo\"\n"
              "repository_id = \"test-repo-id\"\n");
    // One trivial package so scanByNameDir has something to read.
    writeFile(root / "by-name" / "he" / "hello" / "package.toml",
              "format_version = 1\n"
              "description = \"hello\"\n");
    writeFile(root / "by-name" / "he" / "hello" / "versions" / "1.0.0.toml",
              "[artifact]\n"
              "filename = \"hello-1.0.0.pkg.tar.zst\"\n"
              "url = \"file:///tmp/hello-1.0.0.pkg.tar.zst\"\n"
              "sha256 = \"deadbeef\"\n");
}

}  // namespace

int main() {
    fs::path tmp = fs::temp_directory_path() /
                   ("meow-secpol-" + std::to_string(::getpid()));
    fs::path home = tmp / "home";
    fs::path repo = tmp / "repo";
    fs::create_directories(home);
    buildUnsignedRepo(repo);

    // Isolate HOME so cache/keys never touch the real user environment.
    setenv("HOME", home.c_str(), 1);

    std::string url = "file://" + repo.string();

    // 1. Default policy: unsigned repo loads (legacy warn-and-continue).
    {
        repository::setSecurityPolicy(repository::SecurityPolicy{});  // require=false
        bool ok = false;
        try {
            auto r = repository::openRepository(url);
            ok = (r.id == "test-repo-id");
        } catch (const std::exception& e) {
            std::cout << "  (unexpected: " << e.what() << ")\n";
        }
        expectPass("unsigned repo loads when signature not required", ok);
    }

    // 2. Require policy: unsigned repo is rejected with InvalidSignature.
    {
        repository::SecurityPolicy p;
        p.requireRepositorySignature = true;
        repository::setSecurityPolicy(p);
        bool rejected = false;
        try {
            (void)repository::openRepository(url);
        } catch (const error::MeowError& e) {
            rejected = (e.code == error::ErrorCode::InvalidSignature);
        } catch (...) {
        }
        expectPass("unsigned repo rejected when signature required", rejected);
    }

    // Restore default so later state is clean.
    repository::setSecurityPolicy(repository::SecurityPolicy{});

    std::error_code ec;
    fs::remove_all(tmp, ec);

    if (failures == 0) {
        std::cout << "All security-policy checks passed\n";
        return 0;
    }
    std::cout << failures << " security-policy check(s) failed\n";
    return 1;
}
