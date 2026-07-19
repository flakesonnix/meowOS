#ifndef MEOWOS_DOCTOR_H
#define MEOWOS_DOCTOR_H

#include <string>
#include <vector>

#include <meow/config/config.hpp>
#include <meow/database/database.hpp>
#include <meow/repository/repository.hpp>
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
// (optionally loaded) repository. `repo` may be null when the repository
// could not be opened (the trust/expiry checks then report the failure
// instead of throwing).
Diagnosis diagnose(const config::Config& cfg,
                    database::Database& db,
                    const repository::Repository* repo);

// Runs security-focused checks (keys, trust chain, cache, lockfile, hooks
// policy). Read-only: never mutates state.
Diagnosis diagnoseSecurity(const config::Config& cfg,
                           database::Database& db,
                           const repository::Repository* repo,
                           const hooks::HookPolicy& policy);

// Prints a human-readable report. Exits non-zero semantics are the caller's
// responsibility (see errorCount()).
void printReport(const Diagnosis& diag, std::ostream& out);

// Prints a machine-readable JSON report for bug reports / CI.
void printJson(const Diagnosis& diag, std::ostream& out);

}

#endif
