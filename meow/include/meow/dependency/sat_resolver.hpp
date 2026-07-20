#ifndef MEOWOS_DEPENDENCY_SAT_RESOLVER_H
#define MEOWOS_DEPENDENCY_SAT_RESOLVER_H

#include <meow/dependency/iresolver.hpp>
#include <meow/sat/solver.hpp>

namespace meow::dependency {

// Resolver backend that delegates all constraint reasoning to the SAT engine.
// It performs NO propagation or search of its own: it builds a ResolveRequest
// into a package graph, asks GraphTranslator for CNF, runs the Solver, and maps
// the resulting Assignment back to ResolvedPackages. Every package-domain rule
// lives in GraphTranslator; every boolean rule lives in the Solver.
class SatResolver : public IResolver {
public:
    // `solver` must outlive the resolver or be a stable reference. Defaults to
    // a built-in DPLL solver if null is passed.
    explicit SatResolver(sat::Solver* solver = nullptr) : solver_(solver) {}

    ResolveResult resolve(const repository::Repository& repo,
                          const ResolveRequest& req) override;

private:
    sat::Solver* solver_;
};

}  // namespace meow::dependency

#endif  // MEOWOS_DEPENDENCY_SAT_RESOLVER_H
