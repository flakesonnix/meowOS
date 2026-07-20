#include <cassert>
#include <iostream>

#include <meow/sat/problem.hpp>
#include <meow/sat/solver.hpp>

using namespace meow::sat;

namespace {

Problem unit(const std::string& name) {
    Problem p;
    p.declare(name);
    return p;
}

// Build a problem from raw clauses given as literal lists (variable ids are
// 1-based; the translator owns naming). Helper for pure-solver tests.
Problem fromClauses(std::vector<std::vector<Literal>> cls) {
    Problem p;
    // Ensure variables 1..max are declared so Assignment is sized.
    int maxVar = 0;
    for (auto& cl : cls)
        for (Literal lit : cl) maxVar = std::max(maxVar, std::abs(lit));
    for (int v = 1; v <= maxVar; ++v) p.declare("v" + std::to_string(v));
    for (auto& cl : cls) p.add(Clause{std::move(cl)});
    return p;
}

}  // namespace

int main() {
    DpllSolver solver;

    // --- Test 1: single unit clause A -> SAT, A true ---
    {
        Problem p = unit("A");
        p.add(Clause{{1}});  // (A)
        auto r = solver.solve(p);
        assert(r.satisfiable);
        assert(r.assignment.isAssigned(1) && r.assignment.get(1));
        std::cout << "PASS unit (A) -> SAT, A=true\n";
    }

    // --- Test 2: A and ¬A -> UNSAT ---
    {
        auto p = fromClauses({{1}, {-1}});
        auto r = solver.solve(p);
        assert(!r.satisfiable);
        std::cout << "PASS (A) ∧ (¬A) -> UNSAT\n";
    }

    // --- Test 3: A → B, with A forced -> A=true, B=true ---
    {
        auto p = fromClauses({{-1, 2}, {1}});  // (¬A ∨ B), (A)
        auto r = solver.solve(p);
        assert(r.satisfiable);
        assert(r.assignment.get(1) == true);
        assert(r.assignment.get(2) == true);
        std::cout << "PASS (¬A∨B)∧(A) -> A=true B=true\n";
    }

    // --- Test 4: A conflicts B, both forced -> UNSAT ---
    {
        auto p = fromClauses({{-1, -2}, {1}, {2}});  // (¬A ∨ ¬B), (A), (B)
        auto r = solver.solve(p);
        assert(!r.satisfiable);
        std::cout << "PASS conflict (¬A∨¬B)∧(A)∧(B) -> UNSAT\n";
    }

    // --- Test 5: pure literal elimination (A free, B=¬A forced) ---
    {
        // (¬A ∨ B) and (B) forces B=true; A never constrained -> pure, pick false
        auto p = fromClauses({{-1, 2}, {2}});
        auto r = solver.solve(p);
        assert(r.satisfiable);
        assert(r.assignment.get(2) == true);
        std::cout << "PASS pure literal -> SAT\n";
    }

    std::cout << "\nAll DPLL solver tests passed.\n";
    return 0;
}
