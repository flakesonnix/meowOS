#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include <meow/repository/repository.hpp>
#include <meow/sat/translate.hpp>
#include <meow/sat/problem.hpp>
#include <meow/sat/clause.hpp>

using namespace meow;

namespace {

repository::Repository makeRepo() {
    repository::Repository repo;
    repo.name = "test";

    auto pkg = [&](const std::string& name,
                   std::vector<std::string> depends = {},
                   std::vector<std::string> conflicts = {},
                   std::vector<std::string> provides = {}) {
        repository::RepositoryPackage p;
        p.name = types::PackageName{name};
        for (auto& d : depends) p.depends.push_back({types::PackageName{d}, {}});
        for (auto& c : conflicts) p.conflicts.push_back(types::PackageName{c});
        for (auto& pr : provides) p.provides.push_back(types::PackageName{pr});
        repository::RepositoryVersion rv;
        rv.version = types::PackageVersion{"1.0"};
        p.versions.push_back(rv);
        repo.packages.push_back(std::move(p));
    };

    pkg("A", {"B"});        // A depends on B
    pkg("B");
    pkg("foo", {}, {"bar"}); // foo conflicts bar
    pkg("bar");
    pkg("libssl", {}, {}, {"ssl"});  // provides virtual ssl
    return repo;
}

}  // namespace

int main() {
    auto repo = makeRepo();
    sat::GraphTranslator translator(repo);

    // --- Test 1: A depends B encodes (¬A ∨ B) ---
    auto problem = translator.translate({"A"});
    sat::Variable a = problem.lookup("A");
    sat::Variable b = problem.lookup("B");
    assert(a != 0 && b != 0);

    bool foundDep = false;
    for (const auto& cl : problem.clauses()) {
        if (cl.literals.size() == 2 && cl.literals[0] == -a && cl.literals[1] == b) {
            foundDep = true;
        }
    }
    assert(foundDep && "A depends B must encode clause (¬A ∨ B)");
    std::cout << "PASS depends -> implies clause\n";

    // --- Test 2: foo conflicts bar encodes (¬foo ∨ ¬bar) ---
    auto problem2 = translator.translate({"foo"});
    sat::Variable foo = problem2.lookup("foo");
    sat::Variable bar = problem2.lookup("bar");
    bool foundConf = false;
    for (const auto& cl : problem2.clauses()) {
        if (cl.literals.size() == 2 && cl.literals[0] == -foo && cl.literals[1] == -bar) {
            foundConf = true;
        }
    }
    assert(foundConf && "foo conflicts bar must encode clause (¬foo ∨ ¬bar)");
    std::cout << "PASS conflict -> conflict clause\n";

    // --- Test 3: root A is forced via unit clause (A) ---
    bool foundRoot = false;
    for (const auto& cl : problem.clauses()) {
        if (cl.literals.size() == 1 && cl.literals[0] == a) {
            foundRoot = true;
        }
    }
    assert(foundRoot && "root A must be forced by unit clause (A)");
    std::cout << "PASS root -> unit clause\n";

    // --- Test 4: provides declares a variable for the virtual name ---
    auto problem3 = translator.translate({"libssl"});
    assert(problem3.has("ssl") && "virtual provide name must be declared");
    std::cout << "PASS provides -> virtual variable\n";

    // --- Test 5: determinism (same input -> same variable ids) ---
    auto again = translator.translate({"A"});
    assert(again.lookup("A") == a && "variable ids must be deterministic");
    std::cout << "PASS deterministic variable ids\n";

    std::cout << "\nAll SAT translation tests passed.\n";
    return 0;
}
