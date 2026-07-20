// Disk-based (temp-dir) unit tests for secure archive extraction.
// Builds malicious .tar archives in a temp directory and asserts that
// extractPackageContent refuses path-traversal, absolute-path, and
// escaping-symlink entries, while a well-formed package still extracts.
//
// These are regression tests for the v0.5 security hardening pass (audit
// items 3 and 4: libarchive SECURE flags + explicit entry checks).

#include <cassert>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <archive.h>
#include <archive_entry.h>

#include <meow/archive/archive.hpp>
#include <meow/error/error.hpp>

namespace fs = std::filesystem;
using namespace meow;

namespace {

int failures = 0;

void expectPass(const std::string& what, bool ok) {
    std::cout << (ok ? "  PASS: " : "  FAIL: ") << what << "\n";
    if (!ok) ++failures;
}

// Add a regular-file entry to an open archive writer.
void addFile(struct archive* a, const std::string& path,
             const std::string& contents) {
    struct archive_entry* e = archive_entry_new();
    archive_entry_set_pathname(e, path.c_str());
    archive_entry_set_size(e, static_cast<la_int64_t>(contents.size()));
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_perm(e, 0644);
    archive_write_header(a, e);
    if (!contents.empty())
        archive_write_data(a, contents.data(), contents.size());
    archive_entry_free(e);
}

// Add a symlink entry (path -> target) to an open archive writer.
void addSymlink(struct archive* a, const std::string& path,
                const std::string& target) {
    struct archive_entry* e = archive_entry_new();
    archive_entry_set_pathname(e, path.c_str());
    archive_entry_set_filetype(e, AE_IFLNK);
    archive_entry_set_symlink(e, target.c_str());
    archive_entry_set_perm(e, 0777);
    archive_write_header(a, e);
    archive_entry_free(e);
}

// Build a .tar archive at `out`. `build` receives the writer to add entries.
template <typename Fn>
void buildTar(const fs::path& out, Fn build) {
    struct archive* a = archive_write_new();
    archive_write_set_format_pax_restricted(a);
    archive_write_open_filename(a, out.c_str());
    build(a);
    archive_write_close(a);
    archive_write_free(a);
}

// Assert extraction throws (any MeowError) for a malicious archive.
void expectRejected(const std::string& what, const fs::path& archivePath,
                    const fs::path& dest) {
    fs::create_directories(dest);
    bool threw = false;
    try {
        auto ar = archive::openArchive(archivePath);
        archive::extractPackageContent(ar, dest);
    } catch (const error::MeowError&) {
        threw = true;
    } catch (...) {
        threw = true;
    }
    expectPass(what, threw);
}

}  // namespace

int main() {
    fs::path tmp = fs::temp_directory_path() /
                   ("meow-archsec-" + std::to_string(::getpid()));
    fs::create_directories(tmp);

    // 1. "../" escape: files/../../escape.txt must be rejected.
    {
        auto arc = tmp / "dotdot.tar";
        buildTar(arc, [](struct archive* a) {
            addFile(a, "files/../../escape.txt", "pwned");
        });
        expectRejected("rejects ../ path traversal", arc, tmp / "d1");
    }

    // 2. Absolute path inside files/ (files//etc/passwd style) — craft an
    //    entry whose files-relative portion is absolute.
    {
        auto arc = tmp / "abs.tar";
        buildTar(arc, [](struct archive* a) {
            addFile(a, "files//abs/escape.txt", "pwned");
        });
        // The relative part "/abs/escape.txt" is absolute -> rejected.
        expectRejected("rejects absolute path entry", arc, tmp / "d2");
    }

    // 3. Malicious symlink whose target escapes the destination tree.
    {
        auto arc = tmp / "symlink.tar";
        buildTar(arc, [](struct archive* a) {
            addSymlink(a, "files/link", "../../../../etc/passwd");
        });
        expectRejected("rejects escaping symlink target", arc, tmp / "d3");
    }

    // 4. Malicious symlink with an absolute target.
    {
        auto arc = tmp / "abssymlink.tar";
        buildTar(arc, [](struct archive* a) {
            addSymlink(a, "files/link", "/etc/passwd");
        });
        expectRejected("rejects absolute symlink target", arc, tmp / "d4");
    }

    // 5. Valid package still extracts and lands inside the destination.
    {
        auto arc = tmp / "good.tar";
        buildTar(arc, [](struct archive* a) {
            addFile(a, "files/usr/bin/app", "#!/bin/sh\necho hi\n");
            addFile(a, "files/usr/share/doc/readme", "hello\n");
        });
        auto dest = tmp / "d5";
        fs::create_directories(dest);
        bool ok = false;
        try {
            auto ar = archive::openArchive(arc);
            auto extracted = archive::extractPackageContent(ar, dest);
            ok = fs::exists(dest / "usr/bin/app") &&
                 fs::exists(dest / "usr/share/doc/readme") &&
                 extracted.value.size() == 2;
        } catch (const std::exception& e) {
            std::cout << "  (unexpected: " << e.what() << ")\n";
        }
        expectPass("valid package extracts normally", ok);
    }

    std::error_code ec;
    fs::remove_all(tmp, ec);

    if (failures == 0) {
        std::cout << "All archive-security checks passed\n";
        return 0;
    }
    std::cout << failures << " archive-security check(s) failed\n";
    return 1;
}
