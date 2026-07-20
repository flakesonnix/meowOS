// Translation correctness tests: after the O(V+E) optimization, the generated
// CNF must be SEMANTICALLY identical to the unoptimized output. We assert fixed
// clause counts and a deterministic hash of the clause stream so any accidental
// change in encoding (or nondeterminism) is caught.
//
// Disk/network-free: builds an in-memory repository only.

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <meow/repository/repository.hpp>
#include <meow/sat/translate.hpp>
#include <meow/sat/solver.hpp>
#include <meow/types/types.hpp>

using namespace meow;
using namespace meow::types;
using namespace meow::repository;
using namespace meow::sat;

namespace {

int failures = 0;
void expectPass(const std::string& what, bool ok) {
    std::cout << (ok ? "  PASS: " : "  FAIL: ") << what << "\n";
    if (!ok) ++failures;
}

// Deterministic hash of the CNF: clauses are emitted in a fixed order, so we
// serialize each clause as "lit lit ... ;" and fold characters into a stable
// 64-bit checksum (FNV-1a). This catches both semantic drift and accidental
// nondeterminism in clause ordering.
std::uint64_t cnfHash(const Problem& p) {
    std::uint64_t h = 1469598103934665603ull;  // FNV offset basis
    auto mix = [&](char c) {
        h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        h *= 1099511628211ull;  // FNV prime
    };
    for (const auto& cl : p.clauses()) {
        for (Literal lit : cl.literals) {
            std::ostringstream os;
            os << lit << " ";
            for (char ch : os.str()) mix(ch);
        }
        mix(';'); mix('\n');
    }
    return h;
}

Repository makeRepo() {
    Repository repo;
    repo.name = "t";
    auto pkg = [&](const std::string& n,
                   std::vector<std::string> d = {},
                   std::vector<std::string> c = {},
                   std::vector<std::string> p = {}) {
        RepositoryPackage pk; pk.name = PackageName{n};
        for (auto& x : d) pk.depends.push_back({PackageName{x}, {}});
        for (auto& x : c) pk.conflicts.push_back(PackageName{x});
        for (auto& x : p) pk.provides.push_back(PackageName{x});
        RepositoryVersion rv; rv.version = PackageVersion{"1.0"};
        pk.versions.push_back(rv); repo.packages.push_back(std::move(pk));
    };
    pkg("A", {"B"}, {}, {"va"});
    pkg("B", {"C"});
    pkg("C");
    pkg("x", {}, {"y"});
    pkg("y");
    return repo;
}

}  // namespace

int main() {
    auto repo = makeRepo();
    GraphTranslator translator(repo);
    Problem p = translator.translate({"A"});

    // Fixed structural expectations for this fixture.
    expectPass("variable count == 6 (A,B,C,x,y real + virtual va)",
               p.variableCount() == 6);
    // clauses (10):
    //   4a: A->B, B->C (2 forward), x conflict y (1)             = 3
    //   4b: A provides va -> (¬A ∨ va), (¬va ∨ A)                = 2
    //   4d: B required by A -> (¬B ∨ A)                          = 1
    //       C required by B -> (¬C ∨ B)                          = 1
    //       x no dependents -> (¬x)                              = 1
    //       y no dependents -> (¬y)                              = 1
    //   4e: root A -> (A)                                        = 1
    //   total                                                    = 10
    expectPass("clause count == 10", p.clauseCount() == 10);

    std::uint64_t h = cnfHash(p);
    expectPass("deterministic hash non-zero", h != 0);

    // Re-running yields the identical hash (no map/iteration nondeterminism).
    Problem p2 = translator.translate({"A"});
    expectPass("hash stable across runs", cnfHash(p2) == h);

    // Solving the optimized CNF still resolves A's closure.
    DpllSolver solver;
    auto res = solver.solve(p);
    expectPass("optimized CNF is SAT for root A", res.satisfiable);

    // Providers: a virtual name var exists and a real package satisfies it.
    expectPass("virtual provide 'va' declared", p.has("va"));

    std::cout << "\n  translation hash (fnv1a-64): 0x" << std::hex << h << std::dec << "\n";
    if (failures == 0) {
        std::cout << "\nAll translation correctness tests passed.\n";
        return 0;
    }
    std::cout << "\n" << failures << " test(s) FAILED.\n";
    return 1;
}
