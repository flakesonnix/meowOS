#ifndef MEOWOS_DOCTOR_H
#define MEOWOS_DOCTOR_H

#include <string>
#include <vector>

#include <meow/config/config.hpp>
#include <meow/database/database.hpp>
#include <meow/repository/repository.hpp>
#include <meow/repository/manager.hpp>
#include <meow/hooks/policy.hpp>

namespace meow::doctor {

enum class CheckStatus {
    Ok,
    Warning,
    Error
};

struct Check {
    std::string category;
    std::string name;
    CheckStatus status;
    std::string detail;
};

struct Diagnosis {
    std::vector<Check> checks;
    bool healthy() const;
    int errorCount() const;
    int warningCount() const;
};

// Runs all diagnostic checks against the given configuration, database and
// repository manager. Every configured repository is reported individually
// (trust, identity, expiry, metadata cache); a repository that failed to load
// is reported as a failure rather than aborting the diagnosis. This keeps
// doctor backend-agnostic: it never inspects a single fixed repository.
Diagnosis diagnose(const config::Config& cfg,
                    database::Database& db,
                    const repository::RepositoryManager& manager);

// Runs security-focused checks (keys, trust chain, cache, lockfile, hooks
// policy). Read-only: never mutates state. Reports each configured repository
// via the manager.
Diagnosis diagnoseSecurity(const config::Config& cfg,
                            database::Database& db,
                            const repository::RepositoryManager& manager,
                            const hooks::HookPolicy& policy);

// Prints a human-readable report. Exits non-zero semantics are the caller's
// responsibility (see errorCount()).
void printReport(const Diagnosis& diag, std::ostream& out);

// Prints a machine-readable JSON report for bug reports / CI.
void printJson(const Diagnosis& diag, std::ostream& out);

}

#endif
