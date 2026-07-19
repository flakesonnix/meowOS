#include <meow/doctor/doctor.hpp>

#include <meow/crypto/keystore.hpp>
#include <meow/lock/lockfile.hpp>
#include <meow/verify/verifier.hpp>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>

#include <sys/statvfs.h>

#if !defined(_WIN32)
extern "C" long timezone;
#endif

namespace meow::doctor {

namespace {
    void add(std::vector<Check>& checks, std::string category, std::string name,
             CheckStatus status, std::string detail) {
        checks.push_back(Check{std::move(category), std::move(name), status, std::move(detail)});
    }

    CheckStatus worst(CheckStatus a, CheckStatus b) {
        if (a == CheckStatus::Error || b == CheckStatus::Error) return CheckStatus::Error;
        if (a == CheckStatus::Warning || b == CheckStatus::Warning) return CheckStatus::Warning;
        return CheckStatus::Ok;
    }

    // Parse an RFC3339 "Z" timestamp into a time_t, or 0 if unparseable.
    std::time_t parseRfc3339Z(const std::string& s) {
        if (s.size() < 20 || s.back() != 'Z') return 0;
        std::tm tm{};
        std::istringstream is(s.substr(0, 19));
        is >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        if (is.fail()) return 0;
        tm.tm_isdst = 0;
        // timegm is POSIX but not in std; replicate via mktime minus the
        // local timezone offset to land on UTC.
        auto t = std::mktime(&tm);
        if (t == -1) return 0;
#if defined(_WIN32)
        long tz = _timezone;
#else
        long tz = timezone;
#endif
        return t + tz;
    }

    bool dirWritable(const std::filesystem::path& p) {
        std::error_code ec;
        if (!std::filesystem::exists(p, ec)) {
            std::filesystem::create_directories(p, ec);
            if (ec) return false;
        }
        std::filesystem::path probe = p / ".meow-write-test";
        std::ofstream out(probe);
        bool ok = static_cast<bool>(out);
        std::error_code ignore;
        std::filesystem::remove(probe, ignore);
        return ok;
    }

    double freeSpaceGb(const std::filesystem::path& p) {
        std::error_code ec;
        auto rp = std::filesystem::absolute(p, ec);
        if (ec) rp = std::filesystem::current_path();
        struct statvfs st{};
        if (statvfs(rp.c_str(), &st) != 0) return -1.0;
        double bytes = static_cast<double>(st.f_bavail) * st.f_frsize;
        return bytes / (1024.0 * 1024.0 * 1024.0);
    }
}

bool Diagnosis::healthy() const {
    return errorCount() == 0;
}

int Diagnosis::errorCount() const {
    int n = 0;
    for (const auto& c : checks) if (c.status == CheckStatus::Error) ++n;
    return n;
}

int Diagnosis::warningCount() const {
    int n = 0;
    for (const auto& c : checks) if (c.status == CheckStatus::Warning) ++n;
    return n;
}

Diagnosis diagnose(const config::Config& cfg,
                    database::Database& db,
                    const repository::Repository* repo) {
    std::vector<Check> checks;

    // 1. Configuration
    {
        std::ostringstream detail;
        detail << "root=" << cfg.root.string()
               << " cache=" << cfg.cache.string()
               << " db=" << cfg.database.string()
               << " repos=" << cfg.repositories.size()
               << " workers=" << cfg.downloadWorkers;
        if (cfg.repositories.empty()) {
            add(checks, "config", "repositories configured", CheckStatus::Error,
                "no repositories configured");
        } else {
            add(checks, "config", "configuration loaded", CheckStatus::Ok, detail.str());
        }
    }

    // 2. Database schema
    {
        std::error_code ec;
        bool exists = std::filesystem::exists(cfg.database, ec);
        if (!exists) {
            add(checks, "database", "schema present", CheckStatus::Warning,
                "database file missing (will be created on first operation): " + cfg.database.string());
        } else {
            auto ok = database::checkSchema(db);
            add(checks, "database", "schema present",
                ok ? CheckStatus::Ok : CheckStatus::Error,
                ok ? "tables present" : "missing or corrupt schema tables");
        }
    }

    // 3. Repository trust + identity
    if (repo) {
        add(checks, "repository", "signature trusted", CheckStatus::Ok,
            "repository signature verified (id: " + repo->id + ")");
        if (repo->id.empty()) {
            add(checks, "repository", "repository identity", CheckStatus::Error, "missing repository_id");
        } else {
            add(checks, "repository", "repository identity", CheckStatus::Ok, "repository_id=" + repo->id);
        }
    } else {
        add(checks, "repository", "signature trusted", CheckStatus::Error,
            "repository could not be opened (signature/expiry/identity failure)");
    }

    // 4. Repository expiry
    if (repo && repo->expires) {
        auto exp = parseRfc3339Z(*repo->expires);
        auto now = std::time(nullptr);
        if (exp == 0) {
            add(checks, "repository", "metadata not expired", CheckStatus::Warning,
                "unparseable expires value: " + *repo->expires);
        } else if (exp <= now) {
            add(checks, "repository", "metadata not expired", CheckStatus::Error,
                "repository expired at " + *repo->expires);
        } else {
            double days = std::difftime(exp, now) / 86400.0;
            add(checks, "repository", "metadata not expired", CheckStatus::Ok,
                "expires in " + std::to_string(static_cast<int>(std::ceil(days))) + "d (" + *repo->expires + ")");
        }
    }

    // 5. Cache
    {
        std::error_code ec;
        bool exists = std::filesystem::exists(cfg.cache, ec);
        if (!exists) {
            add(checks, "cache", "cache directory", CheckStatus::Warning,
                "cache missing: " + cfg.cache.string());
        } else if (!dirWritable(cfg.cache)) {
            add(checks, "cache", "cache directory", CheckStatus::Error,
                "cache not writable: " + cfg.cache.string());
        } else {
            add(checks, "cache", "cache directory", CheckStatus::Ok, cfg.cache.string());
        }
        if (repo) {
            std::error_code ec2;
            bool rcache = std::filesystem::exists(repo->cache, ec2);
            add(checks, "cache", "verified metadata cache",
                rcache ? CheckStatus::Ok : CheckStatus::Warning,
                rcache ? repo->cache.string() : "no cached metadata for this repository_id");
        }
    }

    // 6. Lockfile consistency
    {
        std::filesystem::path lockPath = "meow.lock";
        std::error_code ec;
        if (!std::filesystem::exists(lockPath, ec)) {
            add(checks, "lockfile", "lockfile consistency", CheckStatus::Ok, "no lockfile present");
        } else {
            try {
                auto lock = lock::loadLockfile(lockPath);
                int mismatch = 0;
                for (const auto& lp : lock.packages) {
                    auto v = database::installedVersion(db, lp.name);
                    if (!v || v->value != lp.version.value) ++mismatch;
                }
                if (mismatch == 0) {
                    add(checks, "lockfile", "lockfile consistency", CheckStatus::Ok,
                        std::to_string(lock.packages.size()) + " locked packages match installed versions");
                } else {
                    add(checks, "lockfile", "lockfile consistency", CheckStatus::Warning,
                        std::to_string(mismatch) + "/" + std::to_string(lock.packages.size()) +
                        " locked packages do not match the installed database");
                }
            } catch (const std::exception& e) {
                add(checks, "lockfile", "lockfile consistency", CheckStatus::Error,
                    std::string("cannot parse lockfile: ") + e.what());
            }
        }
    }

    // 7. Installed file integrity summary
    {
        auto result = verify::verifyAll(db);
        int total = static_cast<int>(result.missing.size() + result.modified.size());
        if (total == 0) {
            add(checks, "integrity", "installed file integrity", CheckStatus::Ok, "no missing or modified files");
        } else {
            add(checks, "integrity", "installed file integrity", CheckStatus::Warning,
                std::to_string(result.missing.size()) + " missing, " +
                std::to_string(result.modified.size()) + " modified files");
        }
    }

    // 8. Disk space
    {
        double gb = freeSpaceGb(cfg.root);
        if (gb < 0) {
            add(checks, "disk", "free space", CheckStatus::Warning, "unable to determine free space");
        } else if (gb < 0.5) {
            add(checks, "disk", "free space", CheckStatus::Error,
                "low free space: " + std::to_string(gb) + " GB on " + cfg.root.string());
        } else if (gb < 2.0) {
            add(checks, "disk", "free space", CheckStatus::Warning,
                "free space low: " + std::to_string(gb) + " GB on " + cfg.root.string());
        } else {
            add(checks, "disk", "free space", CheckStatus::Ok,
                std::to_string(static_cast<int>(gb)) + " GB free on " + cfg.root.string());
        }
    }

    // 9. Permissions
    {
        CheckStatus st = CheckStatus::Ok;
        std::ostringstream detail;
        if (!dirWritable(cfg.root)) {
            st = worst(st, CheckStatus::Error);
            detail << "root not writable: " << cfg.root.string() << "; ";
        }
        auto keys = crypto::keysDir();
        if (!dirWritable(keys)) {
            st = worst(st, CheckStatus::Error);
            detail << "keys dir not writable: " << keys.string() << "; ";
        }
        if (detail.str().empty()) detail << "root and key store writable";
        add(checks, "permissions", "writability", st, detail.str());
    }

    Diagnosis d;
    d.checks = std::move(checks);
    return d;
}

void printReport(const Diagnosis& diag, std::ostream& out) {
    const char* sym[] = {"\x1b[32m✓\x1b[0m", "\x1b[33m!\x1b[0m", "\x1b[31m✗\x1b[0m"};
    std::string lastCat;
    for (const auto& c : diag.checks) {
        if (c.category != lastCat) {
            out << "\n" << c.category << ":\n";
            lastCat = c.category;
        }
        int idx = static_cast<int>(c.status);
        out << "  " << sym[idx] << " " << c.name;
        if (!c.detail.empty()) out << " — " << c.detail;
        out << "\n";
    }
    out << "\n";
    if (diag.healthy()) {
        out << "meow doctor: system healthy (" << diag.checks.size() << " checks)\n";
    } else {
        out << "meow doctor: " << diag.errorCount() << " error(s), "
            << diag.warningCount() << " warning(s)\n";
    }
}

void printJson(const Diagnosis& diag, std::ostream& out) {
    out << "{\n";
    out << "  \"healthy\": " << (diag.healthy() ? "true" : "false") << ",\n";
    out << "  \"errors\": " << diag.errorCount() << ",\n";
    out << "  \"warnings\": " << diag.warningCount() << ",\n";
    out << "  \"checks\": [\n";
    for (size_t i = 0; i < diag.checks.size(); ++i) {
        const auto& c = diag.checks[i];
        const char* s = c.status == CheckStatus::Ok ? "ok"
                      : c.status == CheckStatus::Warning ? "warning" : "error";
        out << "    {\"category\": \"" << c.category << "\", "
            << "\"name\": \"" << c.name << "\", "
            << "\"status\": \"" << s << "\", "
            << "\"detail\": \"" << c.detail << "\"}"
            << (i + 1 < diag.checks.size() ? "," : "") << "\n";
    }
    out << "  ]\n";
    out << "}\n";
}

}
