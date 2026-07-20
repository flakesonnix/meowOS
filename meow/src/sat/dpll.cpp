#include <meow/sat/solver.hpp>

#include <algorithm>
#include <optional>

namespace meow::sat {

namespace {

// State shared across the recursive DPLL calls.
struct DpllState {
    const Problem& problem;
    Assignment assignment;
    std::vector<Clause> clauses;

    explicit DpllState(const Problem& p)
        : problem(p), clauses(p.clauses()) {
        std::size_t n = p.variableCount() + 1;
        assignment.value.assign(n, false);
        assignment.assigned.assign(n, false);
    }

    // Find the first unassigned variable (lowest id). Returns 0 if all
    // assigned.
    Variable nextUnassigned() const {
        for (Variable v = 1; v < static_cast<Variable>(assignment.assigned.size()); ++v) {
            if (!assignment.assigned[v]) return v;
        }
        return 0;
    }

    // A variable that appears only with one polarity across all clauses is
    // "pure" and may be assigned to satisfy every clause it touches.
    std::optional<std::pair<Variable, bool>> findPureLiteral() const {
        std::vector<int> seen(problem.variableCount() + 1, 0);
        for (const auto& cl : clauses) {
            for (Literal lit : cl.literals) {
                Variable v = std::abs(lit);
                seen[v] += (lit > 0) ? 1 : -1;
            }
        }
        for (Variable v = 1; v < static_cast<Variable>(seen.size()); ++v) {
            if (assignment.assigned[v]) continue;
            if (seen[v] > 0) return std::make_pair(v, true);
            if (seen[v] < 0) return std::make_pair(v, false);
        }
        return std::nullopt;
    }

    // Classify a clause under the current assignment.
    enum class Status { Satisfied, Unsatisfied, Unresolved };

    Status clauseStatus(const Clause& cl) const {
        bool hasUnresolved = false;
        for (Literal lit : cl.literals) {
            Variable v = std::abs(lit);
            bool pol = lit > 0;
            if (assignment.assigned[v]) {
                if (assignment.value[v] == pol) return Status::Satisfied;
            } else {
                hasUnresolved = true;
            }
        }
        if (!hasUnresolved) return Status::Unsatisfied;
        return Status::Unresolved;
    }

    bool allSatisfied() const {
        for (const auto& cl : clauses)
            if (clauseStatus(cl) != Status::Satisfied) return false;
        return true;
    }

    bool hasEmptyUnsat() const {
        for (const auto& cl : clauses)
            if (cl.empty()) return true;
        // A clause with no satisfied and no remaining literal is unsatisfied.
        for (const auto& cl : clauses)
            if (clauseStatus(cl) == Status::Unsatisfied) return true;
        return false;
    }

    // Apply unit propagation: any clause with a single unassigned literal
    // forces that literal true. Repeats until no units remain.
    bool propagate() {
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& cl : clauses) {
                if (clauseStatus(cl) != Status::Unresolved) continue;
                int unassignedCount = 0;
                Literal unit = 0;
                for (Literal lit : cl.literals) {
                    Variable v = std::abs(lit);
                    if (!assignment.assigned[v]) {
                        ++unassignedCount;
                        unit = lit;
                    }
                }
                if (unassignedCount == 1) {
                    Variable v = std::abs(unit);
                    assignment.assigned[v] = true;
                    assignment.value[v] = (unit > 0);
                    changed = true;
                }
            }
        }
        return !hasEmptyUnsat();
    }

    bool search() {
        if (!propagate()) return false;

        // Pure literal elimination.
        while (auto pure = findPureLiteral()) {
            Variable v = pure->first;
            assignment.assigned[v] = true;
            assignment.value[v] = pure->second;
            if (!propagate()) return false;
        }

        if (allSatisfied()) return true;
        if (hasEmptyUnsat()) return false;

        Variable v = nextUnassigned();
        if (v == 0) return allSatisfied();

        // True branch.
        Assignment saved = assignment;
        assignment.assigned[v] = true;
        assignment.value[v] = true;
        if (search()) return true;

        // False branch (restore, then negate).
        assignment = saved;
        assignment.assigned[v] = true;
        assignment.value[v] = false;
        if (search()) return true;

        return false;
    }
};

}  // namespace

SolveResult StubSolver::solve(const Problem&) {
    return SolveResult{false, {}};
}

SolveResult DpllSolver::solve(const Problem& problem) {
    DpllState state(problem);
    SolveResult result;
    result.satisfiable = state.search();
    result.assignment = std::move(state.assignment);
    return result;
}

}  // namespace meow::sat
