#ifndef MEOWOS_SAT_CLAUSE_H
#define MEOWOS_SAT_CLAUSE_H

#include <vector>
#include <string>

namespace meow::sat {

// A literal is a signed integer variable. Positive = the variable is true,
// negative = the variable is false (negated). Variable 0 is never used.
using Literal = int;
using Variable = int;

// A clause is a disjunction of literals (OR). An empty clause is unsatisfiable.
struct Clause {
    std::vector<Literal> literals;

    Clause() = default;
    Clause(std::vector<Literal> lits) : literals(std::move(lits)) {}

    // Helpers for the common case of a binary implication a -> b, encoded as
    // the clause (¬a ∨ b).
    static Clause implies(Variable a, Variable b) {
        return Clause{{-a, b}};
    }

    // a conflict a × b, encoded as (¬a ∨ ¬b).
    static Clause conflict(Variable a, Variable b) {
        return Clause({-a, -b});
    }

    bool empty() const { return literals.empty(); }
};

}  // namespace meow::sat

#endif  // MEOWOS_SAT_CLAUSE_H
