#ifndef MEOWOS_SAT_PROBLEM_H
#define MEOWOS_SAT_PROBLEM_H

#include <string>
#include <unordered_map>
#include <vector>

#include <meow/sat/clause.hpp>

namespace meow::sat {

// A SAT problem in conjunctive normal form (CNF): the conjunction (AND) of a
// set of clauses. Variables are opaque integers; a naming map is kept so the
// solver result can be decoded back into package names without the engine
// knowing anything about packages.
class Problem {
public:
    // Register a named variable, returning its integer id (1-based). Repeated
    // names return the same variable (idempotent).
    Variable declare(const std::string& name);

    // Look up the variable for a name, or 0 if not declared.
    Variable lookup(const std::string& name) const;

    bool has(const std::string& name) const;

    // Add a clause to the problem.
    void add(Clause clause) { clauses_.push_back(std::move(clause)); }

    // The variable naming map (name -> id). Useful for decoding a solution.
    const std::unordered_map<std::string, Variable>& names() const { return names_; }

    const std::vector<Clause>& clauses() const { return clauses_; }

    std::size_t variableCount() const { return nextVar_ - 1; }
    std::size_t clauseCount() const { return clauses_.size(); }

private:
    std::unordered_map<std::string, Variable> names_;
    std::vector<Clause> clauses_;
    Variable nextVar_ = 1;
};

}  // namespace meow::sat

#endif  // MEOWOS_SAT_PROBLEM_H
