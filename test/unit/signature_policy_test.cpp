// Unit tests for repository signature trust enforcement (require-signature
// mode). Extends security_policy_test.cpp with the v0.5 audit follow-up
// gaps:
//   - corrupt .sig file must fail closed as InvalidSignature (not bypass)
//   - empty keyId rejection, even with a present .sig
//   - tampered repository.toml is rejected (signature does not match)
//   - unsigned HTTP repository rejected in strict mode
//   - invalid (corrupted) signature over HTTP backend rejected in strict mode
//
// Builds repositories in a temp directory (and serves them over HTTP via the
// meow-server binary) and exercises openRepository under the security policy.
// Uses a temp HOME so the real user cache/keys are never touched.

#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
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

const char* kRepoToml =
    "format_version = 1\n"
    "name = \"testrepo\"\n"
    "repository_id = \"test-repo-id\"\n";

// Resolved at runtime from MEOW_REPO_ROOT (defaults to ".").
std::string gKeysDir = "<REPO_ROOT>/test/keys";
std::string gServerBin = "<REPO_ROOT>/build/meow-server";
std::string gRepoBin = "<REPO_ROOT>/build/meow-repo";

// Build a valid filesystem repository (repository.toml + one package) WITHOUT
// a signature file.
void buildRepo(const fs::path& root) {
    writeFile(root / "repository.toml", kRepoToml);
    writeFile(root / "by-name" / "he" / "hello" / "package.toml",
              "format_version = 1\n"
              "description = \"hello\"\n");
    writeFile(root / "by-name" / "he" / "hello" / "versions" / "1.0.0.toml",
              "[artifact]\n"
              "filename = \"hello-1.0.0.pkg.tar.zst\"\n"
              "url = \"file:///tmp/hello-1.0.0.pkg.tar.zst\"\n"
              "sha256 = \"deadbeef\"\n");
}

// Sign repository.toml in `root` with the test release key, producing
// repository.toml.sig. Returns true on success.
bool signRepo(const fs::path& root) {
    std::string cmd = gRepoBin +
        " sign --key " + gKeysDir + "/meow-release.pem"
        " --key-id meow-release --repo " + root.string() + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

// Install the test release public key into the temp HOME trust store so the
// HTTP/filesystem backend can verify signatures.
void installTrust(const fs::path& home) {
    auto dst = home / ".config" / "meow" / "keys";
    fs::create_directories(dst);
    fs::copy_file(gKeysDir + std::string("/meow-release.pub"),
                  dst / "meow-release.pem",
                  fs::copy_options::overwrite_existing);
}

pid_t startServer(const fs::path& root, int port) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child: replace process with the server.
        std::string repo = root.string();
        std::string sport = std::to_string(port);
        execl(gServerBin.c_str(), gServerBin.c_str(), "serve", repo.c_str(),
              "--port", sport.c_str(), (char*)nullptr);
        _exit(127);
    }
    // Give the server a moment to bind.
    usleep(300'000);
    return pid;
}

void stopServer(pid_t pid) {
    if (pid > 0) {
        kill(pid, SIGTERM);
        int st;
        waitpid(pid, &st, 0);
    }
}

}  // namespace

int main() {
    // <REPO_ROOT> placeholder is rewritten by the build via a #define-free
    // approach: resolve relative to the test's CWD (repo root when run from
    // ctest). Provide overrides via env for out-of-tree builds.
    std::string repoRoot = []() -> std::string {
        if (auto* e = std::getenv("MEOW_REPO_ROOT")) return e;
        // Fall back to the repository root derived from this binary's path:
        // <root>/build/meow-unit-signature-policy  ->  <root>
        std::error_code ec;
        auto self = fs::canonical("/proc/self/exe", ec);
        if (!ec) {
            auto p = self.parent_path().parent_path();
            return p.string();
        }
        return ".";
    }();
    if (gKeysDir.find("<REPO_ROOT>") == 0) gKeysDir = repoRoot + "/test/keys";
    if (gServerBin.find("<REPO_ROOT>") == 0) gServerBin = repoRoot + "/build/meow-server";
    if (gRepoBin.find("<REPO_ROOT>") == 0) gRepoBin = repoRoot + "/build/meow-repo";

    fs::path tmp = fs::temp_directory_path() /
                   ("meow-sigpol-" + std::to_string(::getpid()));
    fs::path home = tmp / "home";
    fs::create_directories(home);

    setenv("HOME", home.c_str(), 1);
    installTrust(home);

    // ---- 1. Corrupt .sig fails closed as InvalidSignature --------------
    {
        auto repo = tmp / "repo-corrupt-sig";
        buildRepo(repo);
        // Write a malformed .sig (not valid TOML) so loadSignature throws.
        writeFile(repo / "repository.toml.sig",
                  "this is not = = = valid toml <<<<\n");

        repository::SecurityPolicy p;
        p.requireRepositorySignature = true;
        repository::setSecurityPolicy(p);

        bool rejected = false;
        try {
            (void)repository::openRepository("file://" + repo.string());
        } catch (const error::MeowError& e) {
            rejected = (e.code == error::ErrorCode::InvalidSignature);
        } catch (...) {
        }
        expectPass("corrupt .sig rejected as InvalidSignature", rejected);
        repository::setSecurityPolicy(repository::SecurityPolicy{});
    }

    // ---- 2. Present .sig with empty keyId is rejected in strict mode ----
    {
        auto repo = tmp / "repo-empty-keyid";
        buildRepo(repo);
        // Valid-TOML .sig but empty keyId.
        writeFile(repo / "repository.toml.sig",
                  "algorithm = \"ed25519\"\n"
                  "keyId = \"\"\n"
                  "signature = \"\"\n");

        repository::SecurityPolicy p;
        p.requireRepositorySignature = true;
        repository::setSecurityPolicy(p);

        bool rejected = false;
        try {
            (void)repository::openRepository("file://" + repo.string());
        } catch (const error::MeowError& e) {
            rejected = (e.code == error::ErrorCode::InvalidSignature);
        } catch (...) {
        }
        expectPass("empty keyId rejected when signature required", rejected);
        repository::setSecurityPolicy(repository::SecurityPolicy{});
    }

    // ---- 3. Tampered repository.toml rejected (sig mismatch) -----------
    {
        auto repo = tmp / "repo-tampered";
        buildRepo(repo);
        signRepo(repo);  // produces a valid sig over the current toml
        // Now tamper with the toml; the existing sig no longer matches.
        writeFile(repo / "repository.toml",
                  "format_version = 1\n"
                  "name = \"testrepo-TAMPERED\"\n"
                  "repository_id = \"test-repo-id\"\n");

        repository::SecurityPolicy p;
        p.requireRepositorySignature = true;
        repository::setSecurityPolicy(p);

        bool rejected = false;
        try {
            (void)repository::openRepository("file://" + repo.string());
        } catch (const error::MeowError& e) {
            rejected = (e.code == error::ErrorCode::InvalidSignature);
        } catch (...) {
        }
        expectPass("tampered repository.toml rejected (sig mismatch)", rejected);
        repository::setSecurityPolicy(repository::SecurityPolicy{});
    }

    // ---- 4. Unsigned HTTP repository rejected in strict mode ----------
    {
        auto repo = tmp / "repo-http-unsigned";
        buildRepo(repo);  // no .sig

        pid_t srv = startServer(repo, 18765);
        repository::SecurityPolicy p;
        p.requireRepositorySignature = true;
        repository::setSecurityPolicy(p);

        bool rejected = false;
        try {
            (void)repository::openRepository("http://127.0.0.1:18765");
        } catch (const error::MeowError& e) {
            rejected = (e.code == error::ErrorCode::InvalidSignature);
        } catch (...) {
        }
        expectPass("unsigned HTTP repository rejected when signature required",
                   rejected);
        repository::setSecurityPolicy(repository::SecurityPolicy{});
        stopServer(srv);
    }

    // ---- 5. Invalid (corrupted) signature over HTTP rejected ----------
    {
        auto repo = tmp / "repo-http-badsig";
        buildRepo(repo);
        signRepo(repo);
        // Corrupt the signature bytes so verification fails.
        writeFile(repo / "repository.toml.sig",
                  "algorithm = \"ed25519\"\n"
                  "keyId = \"meow-release\"\n"
                  "signature = \"corruptedbase64!!!\"\n");

        pid_t srv = startServer(repo, 18766);
        repository::SecurityPolicy p;
        p.requireRepositorySignature = true;
        repository::setSecurityPolicy(p);

        bool rejected = false;
        try {
            (void)repository::openRepository("http://127.0.0.1:18766");
        } catch (const error::MeowError& e) {
            rejected = (e.code == error::ErrorCode::InvalidSignature);
        } catch (...) {
        }
        expectPass("invalid signature over HTTP backend rejected", rejected);
        repository::setSecurityPolicy(repository::SecurityPolicy{});
        stopServer(srv);
    }

    repository::setSecurityPolicy(repository::SecurityPolicy{});

    std::error_code ec;
    fs::remove_all(tmp, ec);

    if (failures == 0) {
        std::cout << "All signature-policy checks passed\n";
        return 0;
    }
    std::cout << failures << " signature-policy check(s) failed\n";
    return 1;
}
