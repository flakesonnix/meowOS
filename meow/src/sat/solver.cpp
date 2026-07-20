#include <meow/sat/solver.hpp>

namespace meow::sat {

SolveResult StubSolver::solve(const Problem&) {
    // Deliberately unsatisfiable: the real algorithm has not been written yet.
    // This keeps SAT-dependent callers from producing a false positive before
    // the engine is implemented.
    return SolveResult{false, {}};
}

}  // namespace meow::sat
