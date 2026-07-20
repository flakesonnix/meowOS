#ifndef MEOWOS_SAT_SOLVER_H
#define MEOWOS_SAT_SOLVER_H

#include <vector>

#include <meow/sat/problem.hpp>

namespace meow::sat {

// A partial truth assignment over variable ids (1-based). `assigned[i]` is
// false for unassigned variables; `value[i]` is meaningful only when assigned.
// The solver fills this in; the resolver layer maps it back to packages.
struct Assignment {
    std::vector<bool> value;     // indexed by variable id; index 0 unused
    std::vector<bool> assigned;  // indexed by variable id; index 0 unused

    bool isAssigned(Variable v) const {
        return v < static_cast<Variable>(assigned.size()) && assigned[v];
    }
    bool get(Variable v) const { return value[v]; }
};

// The result of solving a Problem. If `satisfiable`, `assignment` holds a
// complete model (every declared variable assigned). The engine stays unaware
// of what the variables mean.
struct SolveResult {
    bool satisfiable = false;
    Assignment assignment;
};

// A SAT engine. Operates only on Problem/Clause/Literal/Assignment; it knows
// nothing about packages, dependencies, or repositories. All package-domain
// meaning lives in GraphTranslator and SatResolver.
class Solver {
public:
    virtual ~Solver() = default;

    // Solve the given problem. Returns a model if satisfiable.
    virtual SolveResult solve(const Problem& problem) = 0;
};

// Trivial always-fail stub used during the translation-first milestone. It
// rejects every problem so callers cannot silently believe a resolution
// succeeded before the real solver exists.
class StubSolver : public Solver {
public:
    SolveResult solve(const Problem& problem) override;
};

// Classic recursive DPLL with unit propagation and pure-literal elimination.
// No watched literals, no clause learning, no VSIDS — only correctness.
// Branching picks the lowest-id unassigned variable (deterministic).
class DpllSolver : public Solver {
public:
    SolveResult solve(const Problem& problem) override;
};

}  // namespace meow::sat

#endif  // MEOWOS_SAT_SOLVER_H
