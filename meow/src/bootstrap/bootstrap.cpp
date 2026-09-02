#include <chrono>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>

#include <meow/bootstrap/bootstrap.hpp>
#include <meow/config/config.hpp>
#include <meow/database/database.hpp>
#include <meow/dependency/iresolver.hpp>
#include <meow/error/error.hpp>
#include <meow/install/installer.hpp>
#include <meow/log/logger.hpp>
#include <meow/repository/manager.hpp>
#include <meow/repository/resolver.hpp>
#include <meow/types/types.hpp>

namespace meow::bootstrap {

namespace {

void logPhase(const std::string& phase) {
    std::cout << "==> " << phase << "\n";
}

} // namespace

void bootstrapRootFS(const BootstrapOptions& opts,
                     const meow::config::Config& cfg) {
    auto tStart = std::chrono::steady_clock::now();
    auto target = std::filesystem::weakly_canonical(opts.root);
    auto dbPath = target / "var" / "lib" / "meow" / "database.sqlite";

    std::error_code ec;

    // --- Phase 1: Prepare target root ---
    logPhase("Initializing target root");

    if (std::filesystem::exists(target)) {
        if (!std::filesystem::is_empty(target, ec)) {
            if (opts.force) {
                if (!opts.quiet)
                    std::cout << "  (non-empty target, continuing)\n";
            } else {
                throw meow::error::MeowError(
                    meow::error::ErrorCode::TransactionFailed,
                    "target directory is not empty: " + target.string() +
                    "\n  Use --force to bootstrap into a non-empty directory");
            }
        }
    } else {
        std::filesystem::create_directories(target, ec);
    }

    std::filesystem::create_directories(target / "var/lib/meow", ec);

    // --- Ensure ld.so interpreter symlink exists for OpenRC compatibility ---
    // OpenRC binaries are built with interpreter /usr/lib/meow/ld-linux-x86-64.so.2
    // The actual glibc interpreter is at /lib64/ld-linux-x86-64.so.2
    std::filesystem::create_directories(target / "usr/lib/meow", ec);
    std::filesystem::create_symlink("/lib64/ld-linux-x86-64.so.2",
                                    target / "usr/lib/meow/ld-linux-x86-64.so.2", ec);

    // --- Phase 2: Load repositories ---
    logPhase("Loading repositories");

    meow::repository::RepositoryManager manager(cfg);
    auto& repo = manager.mergedRepository();

    if (!opts.quiet) {
        std::cout << "  " << manager.availableCount()
                  << " repositor" << (manager.availableCount() == 1 ? "y" : "ies")
                  << " available\n";
    }

    // --- Phase 3: Determine package list ---
    std::vector<std::string> packageNames = opts.packages;
    if (packageNames.empty())
        packageNames = {"base"};

    // --- Phase 4: Resolve dependencies ---
    logPhase("Resolving dependencies");

    if (opts.verbose) {
        std::cout << "  Target root: " << target << "\n";
        std::cout << "  Requested packages:\n";
        for (const auto& n : packageNames)
            std::cout << "    " << n << "\n";
    }

    auto resolver = meow::dependency::makeResolver(cfg.resolverEngine);
    meow::dependency::ResolveRequest rreq;
    for (const auto& n : packageNames)
        rreq.roots.push_back(meow::types::PackageName{n});
    auto resolution = resolver->resolve(repo, rreq);

    if (!resolution.ok) {
        std::cerr << "Bootstrap failed\n";
        std::cerr << "  Target root: " << target << "\n";
        std::cerr << "  Stage:       Resolving dependencies\n";
        for (const auto& d : resolution.diagnostics)
            std::cerr << "  Reason:      " << d.message << "\n";
        throw meow::error::MeowError(
            meow::error::ErrorCode::TransactionFailed,
            "bootstrap resolution failed");
    }

    std::vector<std::pair<meow::types::PackageName,
                          meow::types::PackageVersion>> selected;
    std::set<std::string> requested;
    for (const auto& p : resolution.packages) {
        selected.emplace_back(p.name, p.version);
        if (p.isRoot) requested.insert(p.name.value);
    }

    std::vector<meow::package::PackageFile> toInstall;
    for (size_t i = 0; i < selected.size(); ++i) {
        const auto& [name, version] = selected[i];
        auto pkg = meow::repository::resolvePackage(repo, name, version);
        toInstall.push_back(std::move(pkg));
    }

    if (opts.verbose) {
        std::cout << "  Resolved " << selected.size() << " packages"
                  << " (" << requested.size() << " requested)\n";
    }

    // --- Phase 5: Install ---
    logPhase("Installing packages");

    auto db = meow::database::openDatabase(dbPath.string());
    size_t totalFiles = 0;
    try {
        meow::install::installPackages(toInstall, requested,
                                       meow::database::InstallReason::Explicit,
                                       target, db);
        for (const auto& pkg : toInstall)
            totalFiles += pkg.files.value.size();
        meow::database::closeDatabase(db);
    } catch (...) {
        meow::database::closeDatabase(db);
        throw;
    }

    // --- Ensure OpenRC runtime is complete (after package install) ---
    // OpenRC binaries have RUNPATH /usr/lib/meow and need glibc libs there.
    // Copy meowOS glibc libs to /usr/lib/meow and ensure libeinfo is available.
    {
        std::filesystem::create_directories(target / "usr/lib/meow", ec);
        // Helper to copy from a source path if it exists.
        auto copyIfExists = [&](const std::filesystem::path& src,
                                const std::filesystem::path& dstRel) {
            if (std::filesystem::exists(src)) {
                auto dst = target / dstRel;
                std::filesystem::create_directories(dst.parent_path(), ec);
                std::filesystem::copy_file(src, dst,
                    std::filesystem::copy_options::overwrite_existing, ec);
            }
        };
        // meowOS glibc - stable path in pkgs (not host /lib)
        auto glibcSrc = std::filesystem::path("pkgs/by-name/gl/glibc/files/usr/lib");
        // If running from a different cwd, try absolute fallback
        if (!std::filesystem::exists(glibcSrc)) {
            glibcSrc = std::filesystem::path("/home/natasha/Dokumente/coding/meowOS/pkgs/by-name/gl/glibc/files/usr/lib");
        }
        copyIfExists(glibcSrc / "libdl.so.2", "usr/lib/meow/libdl.so.2");
        copyIfExists(glibcSrc / "libm.so", "usr/lib/meow/libm.so");
        copyIfExists(glibcSrc / "libm.so.6", "usr/lib/meow/libm.so.6");
        copyIfExists(glibcSrc / "libpthread.so.0", "usr/lib/meow/libpthread.so.0");
        copyIfExists(glibcSrc / "libnss_compat.so.2", "usr/lib/meow/libnss_compat.so.2");
        copyIfExists(glibcSrc / "libc.so.6", "usr/lib/meow/libc.so.6");
        // libeinfo: OpenRC's lib, installed by openrc package at usr/lib/libeinfo.so.1
        // After bootstrap with openrc, it will be at target/usr/lib/libeinfo.so.1
        // Copy it to the RUNPATH location.
        copyIfExists(target / "usr/lib/libeinfo.so.1", "usr/lib/meow/libeinfo.so.1");
        copyIfExists(target / "usr/lib64/libeinfo.so.1", "usr/lib/meow/libeinfo.so.1");
        // Fallback: if openrc not yet installed but we have a staged extract (CI)
        copyIfExists(std::filesystem::path("/tmp/openrc-extract/files/usr/lib/libeinfo.so.1"),
                     "usr/lib/meow/libeinfo.so.1");
        // Ensure the interpreter symlink still points correctly (idempotent)
        std::filesystem::create_directories(target / "usr/lib/meow", ec);
        std::filesystem::create_symlink("/lib64/ld-linux-x86-64.so.2",
                                        target / "usr/lib/meow/ld-linux-x86-64.so.2", ec);
    }

    // --- Phase 6: Finalize ---
    logPhase("Finalizing bootstrap");

    auto tEnd = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(tEnd - tStart).count();

    db = meow::database::openDatabase(dbPath.string());
    auto installed = meow::database::listInstalled(db);
    meow::database::closeDatabase(db);

    std::cout << "\nBootstrap completed successfully\n";
    std::cout << "  Target root: " << target << "\n";
    std::cout << "  Database:    " << dbPath << "\n";
    std::cout << "  Packages:    " << installed.size() << "\n";
    std::cout << "  Files:       " << totalFiles << "\n";
    std::cout << "  Duration:    " << std::fixed << std::setprecision(2)
              << elapsed << " s\n";
}

} // namespace meow::bootstrap
