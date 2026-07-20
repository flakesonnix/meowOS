#ifndef MEOWOS_SAT_TRANSLATE_H
#define MEOWOS_SAT_TRANSLATE_H

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <meow/repository/repository.hpp>
#include <meow/sat/problem.hpp>

namespace meow::sat {

// Per-phase translation timings (microseconds) so the benchmark can see exactly
// where translation cost lives rather than a single opaque number.
struct TranslatePhases {
    std::uint64_t repositoryScan;  // scan packages, build name->index map
    std::uint64_t graphBuild;      // forward/reverse adjacency + conflict lists
    std::uint64_t variableAssign;  // declare variables, name->id map
    std::uint64_t clauseEmit;      // emit all clauses from adjacency
    std::uint64_t total;

    double ms(std::uint64_t us) const { return us / 1000.0; }
};

// Translate a repository package graph into a SAT problem (CNF).
//
//   A depends on B        ->  (¬A ∨ B)
//   A conflicts with C    ->  (¬A ∨ ¬C)
//   A provides V          ->  V is just another name (a variable) the providing
//                             package satisfies; consumers requiring V pull it.
//
// Translation is split into measurable stages: buildGraph() (scan + adjacency),
// assignVariables(), emitClauses(), and a final Problem assembly. The reverse
// dependency map is built once during buildGraph() so clause emission is O(V+E)
// with no nested package scans.
class GraphTranslator {
public:
    explicit GraphTranslator(const repository::Repository& repo) : repo_(repo) {}

    // Build the problem for the given root package names.
    Problem translate(const std::vector<std::string>& roots) {
        TranslatePhases ignore;
        return translateTimed(roots, ignore);
    }

    // Build the problem and report per-phase timings.
    Problem translateTimed(const std::vector<std::string>& roots,
                           TranslatePhases& phases);

    // The set of concrete (real) package names declared during translation.
    // Virtual provide names are excluded; a true variable that names a real
    // package means that package is selected for installation.
    const std::vector<std::string>& realPackages() const { return real_; }

private:
    const repository::Repository& repo_;
    std::vector<std::string> real_;
};

}  // namespace meow::sat

#endif  // MEOWOS_SAT_TRANSLATE_H
