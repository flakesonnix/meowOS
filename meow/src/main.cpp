#include <ctime>
#include <iostream>
#include <set>
#include <string_view>
#include <vector>

#include <meow/error/error.hpp>
#include <meow/config/config.hpp>
#include <meow/log/logger.hpp>
#include <meow/database/database.hpp>
#include <meow/repository/repository.hpp>
#include <meow/repository/version.hpp>
#include <meow/repository/resolver.hpp>
#include <meow/repository/manager.hpp>
#include <meow/repository/security_policy.hpp>
#include <meow/download/queue.hpp>
#include <meow/install/installer.hpp>
#include <meow/dependency/resolver.hpp>
#include <meow/dependency/iresolver.hpp>
#include <meow/remove/remove.hpp>
#include <meow/upgrade/upgrade.hpp>
#include <meow/lock/lockfile.hpp>
#include <meow/verify/verifier.hpp>
#include <meow/doctor/doctor.hpp>
#include <meow/repair/repair.hpp>
#include <meow/sync/sync.hpp>
#include <meow/update/updater.hpp>
#include <meow/crypto/keystore.hpp>

namespace {
    const auto lockfilePath = std::filesystem::path("meow.lock");
    const auto installRoot = std::filesystem::path("/tmp/meow-install");

    void cmdInfo(const meow::repository::Repository& repo, std::string_view name) {
        auto pkg = meow::repository::resolvePackage(repo, meow::types::PackageName{std::string(name)});

        std::cout << "Package      " << pkg.metadata.name.value << "\n"
                  << "Version      " << pkg.metadata.version.value << "\n"
                  << "Architecture " << (pkg.metadata.architecture == meow::types::CpuArch::AMD64 ? "amd64" : "aarch64") << "\n"
                  << "\n"
                  << "Description\n"
                  << "------------\n"
                  << pkg.metadata.description.value << "\n"
                  << "\n"
                  << "Dependencies\n"
                  << "------------\n";

        if (pkg.metadata.dependencies.value.empty()) {
            std::cout << "(none)\n";
        } else {
            for (const auto& dep : pkg.metadata.dependencies.value) {
                std::cout << "  " << dep.name.value;
                for (const auto& c : dep.constraints) {
                    std::cout << " " << c.op << c.version.value;
                }
                std::cout << "\n";
            }
        }

        std::cout << "\n"
                  << "Optional dependencies\n"
                  << "--------------------\n";

        if (pkg.metadata.optionalDependencies.empty()) {
            std::cout << "(none)\n";
        } else {
            for (const auto& od : pkg.metadata.optionalDependencies) {
                std::cout << "  " << od.package.value;
                if (!od.description.empty()) std::cout << "  " << od.description;
                std::cout << "\n";
            }
        }

        std::cout << "\n"
                  << "License: " << pkg.metadata.license << "\n"
                  << "Homepage: " << pkg.metadata.homepage << "\n"
                  << "Maintainer: " << pkg.metadata.maintainer << "\n"
                  << "\n"
                  << "Files\n"
                  << "-----\n";
        for (const auto& f : pkg.files.value) {
            std::cout << "  " << f.string() << "\n";
        }
    }

    void cmdList(const meow::repository::Repository& repo) {
        for (const auto& name : meow::repository::listPackages(repo)) {
            std::cout << name.value << "\n";
        }
    }

    void cmdSearch(const meow::repository::Repository& repo, std::string_view query) {
        for (const auto& name : meow::repository::listPackages(repo)) {
            if (name.value.find(query) != std::string::npos) {
                std::cout << name.value << "\n";
            }
        }
    }

    void cmdInstalled(meow::database::Database& db) {
        auto packages = meow::database::listInstalled(db);
        if (packages.empty()) {
            std::cout << "(no packages installed)\n";
            return;
        }
        for (const auto& pkg : packages) {
            auto ver = meow::database::installedVersion(db, pkg);
            std::cout << pkg.value;
            if (ver) std::cout << " " << ver->value;
            std::cout << "\n";
        }
    }

    // Append-only history log (oldest first). With a package name, restrict to
    // that package.
    void cmdHistory(meow::database::Database& db, const std::string& name) {
        std::vector<meow::database::HistoryEntry> entries =
            name.empty() ? meow::database::packageHistory(db)
                         : meow::database::packageHistory(db, meow::types::PackageName{name});
        if (entries.empty()) {
            std::cout << "(no history)\n";
            return;
        }
        for (const auto& e : entries) {
            std::time_t t = static_cast<std::time_t>(e.timestamp);
            char buf[64];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&t));
            std::cout << buf << " " << e.action << " " << e.package;
            if (!e.version.empty()) std::cout << " " << e.version;
            std::cout << "\n  reason: " << e.reason;
            if (!e.transactionId.empty()) std::cout << "\n  transaction: " << e.transactionId;
            std::cout << "\n";
        }
    }

    // Why a package is installed: its current reason plus the packages that
    // require it (the resolver graph powers richer "provided by" output later).
    void cmdWhy(meow::database::Database& db, const std::string& name) {
        meow::types::PackageName pkg{name};
        if (!meow::database::isInstalled(db, pkg)) {
            std::cout << "(not installed: " << name << ")\n";
            return;
        }
        auto ver = meow::database::installedVersion(db, pkg);
        auto reason = meow::database::installReason(db, pkg);
        std::cout << name;
        if (ver) std::cout << " " << ver->value;
        std::cout << "\n";
        std::cout << "reason: ";
        if (reason) {
            switch (*reason) {
                case meow::database::InstallReason::Explicit:    std::cout << "explicit"; break;
                case meow::database::InstallReason::GroupMember: std::cout << "group"; break;
                case meow::database::InstallReason::Dependency:  std::cout << "dependency"; break;
            }
        } else {
            std::cout << "unknown";
        }
        std::cout << "\n";

        auto requiredBy = meow::database::requiredBy(db, pkg);
        std::cout << "required by:\n";
        if (requiredBy.empty()) {
            std::cout << "  (nothing)\n";
        } else {
            for (const auto& r : requiredBy) {
                std::cout << "  " << r.value << "\n";
            }
        }
    }

    void cmdExplicitlyInstalled(meow::database::Database& db) {
        auto pkgs = meow::database::explicitlyInstalled(db);
        if (pkgs.empty()) {
            std::cout << "(no explicitly-installed packages)\n";
            return;
        }
        for (const auto& p : pkgs) {
            std::cout << p.value << "\n";
        }
    }

    // Explain why a package is present: its install reason, what requires it,
    // and the virtual names it provides. Queries the DB plus repository
    // metadata; no resolution is performed.
    void cmdExplain(const meow::repository::Repository& repo,
                    meow::database::Database& db, const std::string& name) {
        meow::types::PackageName pkg{name};
        auto ver = meow::database::installedVersion(db, pkg);
        auto reason = meow::database::installReason(db, pkg);

        std::cout << name;
        if (ver) std::cout << " " << ver->value;
        std::cout << "\n\n";

        std::cout << "Install reason:\n  ";
        if (reason) {
            switch (*reason) {
                case meow::database::InstallReason::Explicit:    std::cout << "Explicit"; break;
                case meow::database::InstallReason::GroupMember: std::cout << "GroupMember"; break;
                case meow::database::InstallReason::Dependency:  std::cout << "Dependency"; break;
            }
        } else if (meow::database::isInstalled(db, pkg)) {
            std::cout << "unknown";
        } else {
            std::cout << "not installed";
        }
        std::cout << "\n";

        auto requiredBy = meow::database::requiredBy(db, pkg);
        std::cout << "\nRequired by:\n";
        if (requiredBy.empty()) {
            std::cout << "  (nothing)\n";
        } else {
            for (const auto& r : requiredBy) {
                std::cout << "  " << r.value << "\n";
            }
        }

        const auto* rp = meow::repository::findPackage(repo, pkg);
        std::cout << "\nProvides:\n";
        if (rp && !rp->provides.empty()) {
            for (const auto& p : rp->provides) {
                std::cout << "  " << p.value << "\n";
            }
        } else {
            std::cout << "  (nothing)\n";
        }
    }

    // Explain why a package cannot be installed, using the resolver's own
    // diagnostic decision points (no success/failure reimplementation).
    void cmdWhyNot(const meow::repository::Repository& repo,
                   meow::database::Database& db, const std::string& name) {
        std::vector<meow::dependency::ResolveDiagnostic> diags;
        bool ok = meow::dependency::tryResolve(repo, meow::types::PackageName{name}, db, diags);

        if (ok) {
            std::cout << "Can install " << name << "\n";
            return;
        }

        std::cout << "Cannot install " << name << "\n\n";
        for (const auto& d : diags) {
            std::cout << "- ";
            switch (d.kind) {
                case meow::dependency::ResolveDiagnostic::Kind::MissingPackage:
                    std::cout << "missing package: " << d.package.value;
                    break;
                case meow::dependency::ResolveDiagnostic::Kind::VersionConflict:
                    std::cout << "version conflict: " << d.package.value
                              << " requires " << d.requiredVersion
                              << " but available " << d.availableVersion;
                    break;
                case meow::dependency::ResolveDiagnostic::Kind::PackageConflict:
                    std::cout << "conflict: " << d.message;
                    break;
                case meow::dependency::ResolveDiagnostic::Kind::MissingProvider:
                    std::cout << "no provider found for: " << d.package.value;
                    break;
                case meow::dependency::ResolveDiagnostic::Kind::Cycle:
                    std::cout << "dependency cycle: " << d.package.value;
                    break;
            }
            std::cout << "\n";
        }
    }

    // Resolve a dependency closure for several package names, download every
    // artifact in parallel, and return the resolved PackageFiles ready to
    // install. Shared by `meow install <pkg>` and `meow group install <grp>`,
    // so both take the same atomic single-transaction path.
    std::vector<meow::package::PackageFile> resolveAndStage(
        const meow::repository::Repository& repo,
        const meow::config::Config& cfg,
        const std::vector<meow::types::PackageName>& names) {
        std::vector<meow::types::PackageName> closure;
        std::set<std::string> seen;
        for (const auto& name : names) {
            auto nameset = meow::repository::resolveDependencyNames(repo, name);
            for (const auto& n : nameset) {
                if (seen.insert(n.value).second) closure.push_back(n);
            }
        }

        std::vector<meow::download::DownloadTask> tasks;
        for (const auto& name : closure) {
            const auto* rp = meow::repository::findPackage(repo, name);
            if (!rp) {
                throw meow::error::MeowError(meow::error::ErrorCode::PackageNotFound,
                                             "package not found: " + name.value);
            }
            if (rp->versions.empty()) {
                throw meow::error::MeowError(meow::error::ErrorCode::VersionNotFound,
                                             "no version for: " + name.value);
            }
            meow::download::DownloadTask task;
            task.artifact = rp->versions.back().artifact;
            tasks.push_back(std::move(task));
        }

        meow::download::DownloadQueue queue;
        queue.workers = cfg.downloadWorkers;
        meow::log::log(meow::log::LogLevel::Info,
                       "downloading " + std::to_string(tasks.size()) +
                           " artifacts in parallel");
        meow::download::downloadAll(queue, tasks);

        std::vector<meow::package::PackageFile> toInstall;
        for (const auto& name : closure) {
            auto pkg = meow::repository::resolvePackage(repo, name);
            std::cout << "  " << name.value << " " << pkg.metadata.version.value << "\n";
            toInstall.push_back(std::move(pkg));
        }
        return toInstall;
    }

    void cmdGroupList(const meow::config::Config& cfg) {
        if (cfg.groups.empty()) {
            std::cout << "(no groups defined)\n";
            return;
        }
        for (const auto& g : cfg.groups) {
            std::cout << g.name << ":\n";
            for (const auto& p : g.packages)
                std::cout << "  - " << p << "\n";
        }
    }

    void cmdSaveLockfile(
        const std::vector<meow::package::PackageFile>& packages,
        const meow::repository::Repository& repo
    ) {
        meow::lock::Lockfile lock;
        lock.repositoryHash = "dev";

        for (const auto& pkg : packages) {
            const auto* repoPkg = meow::repository::findPackage(repo, pkg.metadata.name);
            if (!repoPkg) continue;

            for (const auto& rv : repoPkg->versions) {
                if (rv.version.value == pkg.metadata.version.value) {
                    meow::lock::LockedPackage lp;
                    lp.name = pkg.metadata.name;
                    lp.version = pkg.metadata.version;
                    lp.artifact = rv.artifact;
                    lock.packages.push_back(std::move(lp));
                    break;
                }
            }
        }

        meow::lock::saveLockfile(lock, lockfilePath);
        meow::log::log(meow::log::LogLevel::Info, "created meow.lock");
    }

    void cmdVerify(meow::database::Database& db) {
        meow::log::log(meow::log::LogLevel::Info, "checking installed packages");
        auto vr = meow::verify::verifyAll(db);
        size_t errors = vr.missing.size() + vr.modified.size();

        if (errors == 0) {
            meow::log::log(meow::log::LogLevel::Info, "all files intact");
        } else {
            for (const auto& f : vr.missing) {
                std::cout << "  \x1b[31m\u2717 " << f.string() << " (missing)\x1b[0m\n";
            }
            for (const auto& f : vr.modified) {
                std::cout << "  \x1b[33m\u2717 " << f.string() << " (modified)\x1b[0m\n";
            }
            meow::log::log(meow::log::LogLevel::Warning,
                std::to_string(errors) + " error" + (errors == 1 ? "" : "s") + " found");
        }
    }
}

int main(int argc, char** argv) {
    // parse global options
    std::string dbPath;
    std::string repositoryOverride;
    std::string configPath;
    int argi = 1;
    while (argi < argc) {
        std::string_view a = argv[argi];
        if (a == "--db-path" && argi + 1 < argc) {
            dbPath = argv[++argi];
            ++argi;
        } else if (a == "--repository" && argi + 1 < argc) {
            repositoryOverride = argv[++argi];
            ++argi;
        } else if (a == "--config" && argi + 1 < argc) {
            configPath = argv[++argi];
            ++argi;
        } else {
            break;
        }
    }

    if (argi >= argc) {
    std::cerr << "usage: meow [--db-path <path>] <command> [args]\n"
              << "  info   <package>\n"
              << "  list\n"
              << "  search <query>\n"
              << "  install [--with-optional] [--optional <name>]... [--locked] <package>\n"
              << "  group <list|install> [name]\n"
              << "  upgrade <package>\n"
              << "  remove <package>\n"
              << "  installed\n"
              << "  verify\n"
              << "  repair [<package>]\n"
              << "  sync\n"
              << "  update [--dry-run]\n"
               << "  owns <file>\n"
               << "  required-by <package>\n"
               << "  history [package]\n"
              << "  why <package>\n"
              << "  explicitly-installed\n"
              << "  explain <package>\n"
              << "  why-not <package>\n"
              << "  keys list\n"
               << "  keys add <file>\n"
               << "  clean\n";
        return 1;
    }

    auto cmd = argv[argi];
    int cmdArgc = argc - argi;
    char** cmdArgv = argv + argi;

    // Handle keys commands early (no repo/db needed)
    if (cmd == std::string_view("keys")) {
        if (cmdArgc < 2) {
            std::cerr << "usage: meow keys list|add <file>\n";
            return 1;
        }
        if (cmdArgv[1] == std::string_view("list")) {
            auto keys = meow::crypto::listTrustedKeys();
            if (keys.empty()) {
                std::cout << "(no trusted keys)\n";
                return 0;
            }
            std::cout << "Trusted keys:\n";
            for (const auto& k : keys) {
                std::cout << "  " << k.id << "\n    " << k.path.string() << "\n";
            }
            return 0;
        }
        if (cmdArgv[1] == std::string_view("add")) {
            if (cmdArgc < 3) {
                std::cerr << "usage: meow keys add <file>\n";
                return 1;
            }
            meow::crypto::addTrustedKey(cmdArgv[2]);
            std::cout << "added key: " << std::filesystem::path(cmdArgv[2]).filename().string() << "\n";
            return 0;
        }
        std::cerr << "unknown keys command: " << cmdArgv[1] << "\n";
        return 1;
    }

    try {
        auto cfg = meow::config::defaultConfig();
        if (!configPath.empty()) {
            cfg = meow::config::loadConfig(configPath);
        }
        if (!repositoryOverride.empty()) {
            cfg.repositories.clear();
            cfg.repositories.push_back(meow::config::RepositoryConfig{
                .id = "default",
                .mirrors = {repositoryOverride},
                .url = repositoryOverride});
        }

        // MEOW_RESOLVER overrides the configured engine so CI can exercise
        // both backends against the same integration suite (legacy / sat / auto).
        if (const char* r = std::getenv("MEOW_RESOLVER")) {
            cfg.resolverEngine = meow::config::parseResolverEngine(r);
        }

        // Apply the repository security policy before any repository is opened.
        // MEOW_REQUIRE_SIGNATURE=1 lets CI/tests enforce without a config file.
        {
            meow::repository::SecurityPolicy policy;
            policy.requireRepositorySignature = cfg.requireRepositorySignature;
            if (const char* e = std::getenv("MEOW_REQUIRE_SIGNATURE")) {
                std::string v(e);
                policy.requireRepositorySignature =
                    (v == "1" || v == "true" || v == "yes");
            }
            meow::repository::setSecurityPolicy(policy);
        }

        meow::repository::RepositoryManager manager(cfg);
        meow::repository::Repository repo = manager.mergedRepository();
        auto db = meow::database::openDatabase(dbPath.empty() ? "" : dbPath);

        // If no repository could be loaded, surface the failure for any
        // command that depends on repository metadata. This keeps a single
        // broken source a loud error while still tolerating failures when at
        // least one healthy repository is available.
        if (manager.repositories().empty() && cmd != "keys" && cmd != "clean") {
            std::cerr << "error: no repository available: "
                      << manager.lastError() << "\n";
            return 1;
        }

        std::string_view cmd = cmdArgv[0];

        // doctor reports every configured repository via the manager; a
        // broken source is surfaced as a check rather than aborting.
        if (cmd == "doctor") {
            meow::log::setLevel(meow::log::LogLevel::Error);
            bool asJson = false;
            bool security = false;
            for (int i = 1; i < cmdArgc; ++i) {
                if (std::string_view(cmdArgv[i]) == "--json") asJson = true;
                else if (std::string_view(cmdArgv[i]) == "--security") security = true;
            }
            if (security) {
                meow::hooks::HookPolicy policy;
                policy.timeout = std::chrono::seconds(cfg.hookTimeout);
                policy.allowNetwork = cfg.hookAllowNetwork;
                auto diag = meow::doctor::diagnoseSecurity(cfg, db, manager, policy);
                if (asJson) meow::doctor::printJson(diag, std::cout);
                else meow::doctor::printReport(diag, std::cout);
                return diag.healthy() ? 0 : 1;
            }
            auto diag = meow::doctor::diagnose(cfg, db, manager);
            if (asJson) {
                meow::doctor::printJson(diag, std::cout);
            } else {
                meow::doctor::printReport(diag, std::cout);
            }
            return diag.healthy() ? 0 : 1;
        }

        meow::log::log(meow::log::LogLevel::Debug, "config loaded, database opened");

        if (cmd == "info") {
            if (cmdArgc < 2) {
                std::cerr << "usage: meow info <package>\n";
                return 1;
            }
            cmdInfo(repo, cmdArgv[1]);
        } else if (cmd == "list") {
            cmdList(repo);
        } else if (cmd == "search") {
            if (cmdArgc < 2) {
                std::cerr << "usage: meow search <query>\n";
                return 1;
            }
            cmdSearch(repo, cmdArgv[1]);
        } else if (cmd == "install") {
            // Flags (any order, before the package name):
            //   --with-optional      install every declared optional dependency
            //   --optional <name>    install a named optional (repeatable)
            //   --locked             install from the lockfile
            bool withOptional = false;
            bool locked = false;
            std::set<meow::types::PackageName> selectedOptional;
            std::string pkgName;
            for (int i = 1; i < cmdArgc; ++i) {
                std::string_view a = cmdArgv[i];
                if (a == "--with-optional") {
                    withOptional = true;
                } else if (a == "--optional") {
                    if (i + 1 >= cmdArgc) {
                        std::cerr << "usage: meow install <package> --optional <name>\n";
                        return 1;
                    }
                    selectedOptional.insert(meow::types::PackageName{std::string(cmdArgv[++i])});
                } else if (a == "--locked") {
                    locked = true;
                } else {
                    pkgName = std::string(a);
                }
            }
            if (pkgName.empty()) {
                std::cerr << "usage: meow install [--with-optional] [--optional <name>]... [--locked] <package>\n";
                return 1;
            }

            meow::package::PackageMetadata meta;
            std::vector<meow::package::PackageFile> toInstall;
            meow::lock::Lockfile lock;
            std::set<std::string> requested;

            if (locked) {
                meow::log::log(meow::log::LogLevel::Info, "using lockfile");
                lock = meow::lock::loadLockfile(lockfilePath);

                const auto* lockedPkg = meow::lock::findLockedPackage(lock, meow::types::PackageName{pkgName});
                if (!lockedPkg) {
                    std::cerr << "package not found in lockfile: " << pkgName << "\n";
                    return 1;
                }
                meta = meow::repository::resolveLockedPackage(lock, meow::types::PackageName{pkgName}).metadata;
                auto tree = meow::dependency::resolveDependencies(repo, meta, db, &lock);

                meow::log::log(meow::log::LogLevel::Info, "resolving dependencies from lockfile");
                for (const auto& name : tree.packages) {
                    auto pkg = meow::repository::resolveLockedPackage(lock, name);
                    std::cout << "  " << name.value << " " << pkg.metadata.version.value << "\n";
                    toInstall.push_back(std::move(pkg));
                    requested.insert(name.value);
                }
            } else {
                // Resolve through the configured backend (legacy DFS or SAT).
                // Optional dependencies are promoted to roots inside the
                // resolver; the engine stays unaware of "optional".
                auto resolver = meow::dependency::makeResolver(cfg.resolverEngine);
                meow::dependency::ResolveRequest rreq;
                rreq.roots.push_back(meow::types::PackageName{pkgName});
                rreq.includeAllOptional = withOptional;
                rreq.selectedOptional = selectedOptional;
                auto resolution = resolver->resolve(repo, rreq);

                if (!resolution.ok) {
                    std::cerr << "resolution failed:\n";
                    for (const auto& d : resolution.diagnostics)
                        std::cerr << "  " << d.message << "\n";
                    return 1;
                }

                std::vector<meow::types::PackageName> roots;
                for (const auto& p : resolution.packages)
                    roots.push_back(p.name);

                meow::log::log(meow::log::LogLevel::Info, "resolving dependency names");
                toInstall = resolveAndStage(repo, cfg, roots);
                for (const auto& r : rreq.roots) requested.insert(r.value);
            }

            meow::log::log(meow::log::LogLevel::Info, "installing packages");
            meow::install::installPackages(toInstall, requested,
                                           meow::database::InstallReason::Explicit,
                                           installRoot, db);

            if (!locked) {
                cmdSaveLockfile(toInstall, repo);
            }

            std::cout << "\ndone\n";
        } else if (cmd == "group") {
            if (cmdArgc < 2) {
                std::cerr << "usage: meow group <list|install> [name]\n";
                return 1;
            }
            std::string sub = cmdArgv[1];
            if (sub == "list") {
                cmdGroupList(cfg);
            } else if (sub == "install") {
                if (cmdArgc < 3) {
                    std::cerr << "usage: meow group install <name>\n";
                    return 1;
                }
                std::string groupName = cmdArgv[2];
                const meow::config::PackageGroup* grp = nullptr;
                for (const auto& g : cfg.groups) {
                    if (g.name == groupName) { grp = &g; break; }
                }
                if (!grp) {
                    std::cerr << "group not found: " << groupName << "\n";
                    return 1;
                }
                std::vector<meow::types::PackageName> members;
                for (const auto& p : grp->packages)
                    members.push_back(meow::types::PackageName{p});

                // Expand the group to its members and install them all in one
                // atomic transaction. A group is an expansion alias, not a
                // package identity: the database records the individual
                // packages, never a synthetic "group" entity.
                meow::log::log(meow::log::LogLevel::Info,
                               "expanding group " + groupName);
                auto toInstall = resolveAndStage(repo, cfg, members);

                meow::log::log(meow::log::LogLevel::Info,
                               "installing group " + groupName);
                std::set<std::string> requested;
                for (const auto& p : grp->packages) requested.insert(p);
                meow::install::installPackages(toInstall, requested,
                                               meow::database::InstallReason::GroupMember,
                                               installRoot, db);
                cmdSaveLockfile(toInstall, repo);
                std::cout << "\ndone\n";
            } else {
                std::cerr << "unknown group command: " << sub << "\n";
                return 1;
            }
        } else if (cmd == "upgrade") {
            if (cmdArgc < 2) {
                std::cerr << "usage: meow upgrade <package>\n";
                return 1;
            }
            auto result = meow::upgrade::upgradePackage(repo, db, meow::types::PackageName{cmdArgv[1]}, installRoot);
            if (result.upToDate) {
                std::cout << cmdArgv[1] << " " << result.oldVersion->value << " is already up to date\n";
            }
        } else if (cmd == "remove") {
            if (cmdArgc < 2) {
                std::cerr << "usage: meow remove <package>\n";
                return 1;
            }
            meow::remove::removePackage(meow::types::PackageName{cmdArgv[1]}, db);
        } else if (cmd == "verify") {
            cmdVerify(db);
        } else if (cmd == "repair") {
            if (cmdArgc >= 2) {
                meow::log::log(meow::log::LogLevel::Info, std::string("checking ") + cmdArgv[1]);
                auto result = meow::repair::repairPackage(repo, db, meow::types::PackageName{cmdArgv[1]}, installRoot);
                if (result.ok) {
                    std::cout << "  " << cmdArgv[1] << " OK\n";
                } else {
                    for (const auto& f : result.repaired) {
                        std::cout << "  \x1b[32m\u2713 " << f << "\x1b[0m\n";
                    }
                }
            } else {
                meow::log::log(meow::log::LogLevel::Info, "checking installed packages");
                auto result = meow::repair::repairAll(repo, db, installRoot);
                for (const auto& f : result.repaired) {
                    std::cout << "  \x1b[32m\u2713 " << f << "\x1b[0m\n";
                }
            }
        } else if (cmd == "sync") {
            std::cout << "Synchronizing repositories...\n";
            for (const auto& s : manager.repositories()) {
                std::cout << "  " << s.config.id << "  "
                          << statusLabel(s.status) << "\n";
                if (s.status == meow::repository::RepositoryStatus::Available) {
                    std::cout << "    \x1b[32m\u2713 " << s.attempts.back().url
                              << " Available\x1b[0m\n";
                } else {
                    for (const auto& a : s.attempts) {
                        std::string detail =
                            a.error != meow::error::ErrorCode::Internal
                                ? (" " + std::string(statusLabel(a.status)))
                                : "";
                        std::cout << "    \x1b[31m\u2717 " << a.url
                                  << detail << "\x1b[0m\n";
                    }
                }
            }

            auto updates = meow::sync::checkUpdates(repo, db);
            if (updates.empty()) {
                meow::log::log(meow::log::LogLevel::Info, "all packages up to date");
            } else {
                meow::log::log(meow::log::LogLevel::Info, "updates available");
                for (const auto& u : updates) {
                    std::cout << "  " << u.name.value
                              << "  " << u.installed.value
                              << " → " << u.available.value << "\n";
                }
                meow::log::log(meow::log::LogLevel::Info,
                    std::to_string(updates.size()) + " update" + (updates.size() == 1 ? "" : "s") + " available");
            }
        } else if (cmd == "update") {
            if (cmdArgc >= 2 && cmdArgv[1] == std::string_view("--dry-run")) {
                auto updates = meow::sync::checkUpdates(repo, db);
                if (updates.empty()) {
                    std::cout << "All packages up to date\n";
                } else {
                    std::cout << "Would update:\n";
                    for (const auto& u : updates) {
                        std::cout << "  " << u.name.value
                                  << "  " << u.installed.value
                                  << " -> " << u.available.value << "\n";
                    }
                    std::cout << "No changes made\n";
                }
            } else {
                auto result = meow::update::updateAll(repo, db);
                if (!result.updated.empty()) {
                    std::cout << "Updated:\n";
                    for (const auto& n : result.updated) {
                        std::cout << "  " << n.value << "\n";
                    }
                }
                if (!result.failed.empty()) {
                    for (const auto& f : result.failed) {
                        std::cout << "  \x1b[31m" << f.name.value << " FAILED: " << f.reason << "\x1b[0m\n";
                    }
                }
                if (result.updated.empty() && result.failed.empty()) {
                    std::cout << "All packages up to date\n";
                }
            }
        } else if (cmd == "owns") {
            if (cmdArgc < 2) {
                std::cerr << "usage: meow owns <file>\n";
                return 1;
            }
            auto owner = meow::database::owns(db, std::filesystem::path(cmdArgv[1]));
            if (owner) {
                auto ver = meow::database::installedVersion(db, *owner);
                std::cout << owner->value;
                if (ver) std::cout << " " << ver->value;
                std::cout << "\n";
            } else {
                std::cout << "(no package owns this file)\n";
            }
        } else if (cmd == "required-by") {
            if (cmdArgc < 2) {
                std::cerr << "usage: meow required-by <package>\n";
                return 1;
            }
            auto deps = meow::database::requiredBy(db, meow::types::PackageName{cmdArgv[1]});
            if (deps.empty()) {
                std::cout << "(nothing depends on " << cmdArgv[1] << ")\n";
            } else {
                for (const auto& d : deps) {
                    std::cout << d.value << "\n";
                }
            }
        } else if (cmd == "history") {
            std::string name;
            if (cmdArgc >= 2) name = cmdArgv[1];
            cmdHistory(db, name);
        } else if (cmd == "why") {
            if (cmdArgc < 2) {
                std::cerr << "usage: meow why <package>\n";
                return 1;
            }
            cmdWhy(db, cmdArgv[1]);
        } else if (cmd == "explicitly-installed") {
            cmdExplicitlyInstalled(db);
        } else if (cmd == "explain") {
            if (cmdArgc < 2) {
                std::cerr << "usage: meow explain <package>\n";
                return 1;
            }
            cmdExplain(repo, db, cmdArgv[1]);
        } else if (cmd == "why-not") {
            if (cmdArgc < 2) {
                std::cerr << "usage: meow why-not <package>\n";
                return 1;
            }
            cmdWhyNot(repo, db, cmdArgv[1]);
        } else if (cmd == "installed") {
            cmdInstalled(db);
        } else if (cmd == "clean") {
            meow::repository::clearRepositoryCache();
            std::cout << "cache cleared\n";
        } else {
            std::cerr << "unknown command: " << cmd << "\n";
            return 1;
        }
    } catch (const meow::error::MeowError& e) {
        std::cerr << meow::error::formatError(e) << "\n";
        return 1;
    }

    return 0;
}
