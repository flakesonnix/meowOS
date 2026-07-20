// SAT resolution benchmark. Reports per-phase translation / solver / mapping
// timings plus problem size for a range of synthetic repository shapes. These
// numbers establish a baseline for DPLL before any CDCL / watched-literal
// optimization and let us compare the SAT backend against the legacy resolver
// on the same structures.
//
// Run: meow-bench [--csv]
//   default: human-readable table
//   --csv:    machine-readable CSV on stdout (one row per fixture)
// Disk/network-free: repositories are generated in memory.

#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <set>
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

// ---------------------------------------------------------------------------
// Fixture generators
// ---------------------------------------------------------------------------

// A single linear chain p0 -> p1 -> ... -> p(n-1). Every package depends on the
// next, so resolution must walk the whole chain.
Repository linearChain(int n) {
    Repository repo;
    repo.name = "linear";
    for (int i = 0; i < n; ++i) {
        RepositoryPackage p;
        p.name = PackageName{"p" + std::to_string(i)};
        if (i + 1 < n) p.depends.push_back(PackageName{"p" + std::to_string(i + 1)});
        RepositoryVersion rv; rv.version = PackageVersion{"1.0"};
        p.versions.push_back(rv);
        repo.packages.push_back(std::move(p));
    }
    return repo;
}

// A deep dependency chain (alias of linearChain, kept as a named fixture).
Repository deepChain(int depth) { return linearChain(depth); }

// Wide graph: a balanced tree of depth `depth` with branching factor `fanout`.
// Shallow roots fan out broadly but each node has a single parent, so DPLL
// resolves it via unit propagation rather than exponential search. Exercises
// high in-degree / out-degree fan-out without hiding a solver blow-up.
Repository wideGraph(int depth, int fanout) {
    Repository repo;
    repo.name = "wide";
    auto mk = [&](const std::string& n) {
        RepositoryPackage p; p.name = PackageName{n};
        RepositoryVersion rv; rv.version = PackageVersion{"1.0"};
        p.versions.push_back(rv); repo.packages.push_back(std::move(p));
        return p.name.value;
    };
    std::vector<std::string> level{"root"};
    mk("root");
    for (int d = 0; d < depth; ++d) {
        std::vector<std::string> next;
        for (const auto& parent : level) {
            for (int i = 0; i < fanout; ++i) {
                std::string child = parent + "_c" + std::to_string(i);
                mk(child);
                for (auto& pkg : repo.packages) {
                    if (pkg.name.value == parent) {
                        pkg.depends.push_back(PackageName{child});
                        break;
                    }
                }
                next.push_back(child);
            }
        }
        level = std::move(next);
    }
    return repo;
}

// Many providers: a single virtual "service" is provided by `count` packages;
// one root requires "service". Exercises provider expansion / selection.
Repository manyProviders(int count) {
    Repository repo;
    repo.name = "providers";
    auto mk = [&](const std::string& n, bool provides = false) {
        RepositoryPackage p; p.name = PackageName{n};
        if (provides) p.provides.push_back(PackageName{"service"});
        RepositoryVersion rv; rv.version = PackageVersion{"1.0"};
        p.versions.push_back(rv); repo.packages.push_back(std::move(p));
    };
    mk("root");
    repo.packages.back().depends.push_back(PackageName{"service"});
    for (int i = 0; i < count; ++i) mk("prov" + std::to_string(i), true);
    return repo;
}

// Many virtuals with multiple providers each. Creates `num` virtual names
// (virt_0..virt_{num-1}), each with `providersPer` provider packages, plus a
// single root that depends on ALL virtuals. Exercises large-scale provider
// encoding.
Repository manyVirtuals(int num, int providersPer) {
    Repository repo;
    repo.name = "many-virtuals";
    auto mk = [&](const std::string& n,
                  const std::vector<std::string>& deps = {},
                  const std::vector<std::string>& provides = {}) {
        RepositoryPackage p; p.name = PackageName{n};
        for (auto& d : deps) p.depends.push_back(PackageName{d});
        for (auto& pr : provides) p.provides.push_back(PackageName{pr});
        RepositoryVersion rv; rv.version = PackageVersion{"1.0"};
        p.versions.push_back(rv); repo.packages.push_back(std::move(p));
    };
    mk("root");
    int id = 0;
    for (int v = 0; v < num; ++v) {
        std::string vname = "virt_" + std::to_string(v);
        mk("root", {vname});  // append dep on root
        for (int p = 0; p < providersPer; ++p) {
            mk("pv" + std::to_string(id++), {}, {vname});
        }
    }
    // Remove duplicate dependency entries from root
    auto& rootPkg = repo.packages[0];
    std::set<std::string> seen;
    std::vector<PackageName> uniq;
    for (auto& d : rootPkg.depends) {
        if (seen.insert(d.value).second) uniq.push_back(d);
    }
    rootPkg.depends = std::move(uniq);
    return repo;
}

// Many versions: `packages` packages, each with `versions` distinct versions,
// chained by dependency. Exercises version-space explosion.
Repository manyVersions(int packages, int versions) {
    Repository repo;
    repo.name = "many-versions";
    for (int i = 0; i < packages; ++i) {
        RepositoryPackage p;
        p.name = PackageName{"v" + std::to_string(i)};
        if (i + 1 < packages) p.depends.push_back(PackageName{"v" + std::to_string(i + 1)});
        for (int v = 1; v <= versions; ++v) {
            RepositoryVersion rv; rv.version = PackageVersion{std::to_string(v) + ".0"};
            p.versions.push_back(rv);
        }
        repo.packages.push_back(std::move(p));
    }
    return repo;
}

// Dense conflicts: `n` packages where every pair conflicts. The conflict clause
// set is O(n^2); stresses clause emission and the solver's conflict handling.
Repository denseConflicts(int n) {
    Repository repo;
    repo.name = "dense-conflicts";
    for (int i = 0; i < n; ++i) {
        RepositoryPackage p; p.name = PackageName{"c" + std::to_string(i)};
        for (int j = 0; j < n; ++j) {
            if (i == j) continue;
            p.conflicts.push_back(PackageName{"c" + std::to_string(j)});
        }
        RepositoryVersion rv; rv.version = PackageVersion{"1.0"};
        p.versions.push_back(rv); repo.packages.push_back(std::move(p));
    }
    return repo;
}

// Deterministic random DAG. Seeded mt19937_64 so the same (nodes, edgeProb,
// seed) always yields the same graph — reproducible benchmark fixtures.
Repository randomDag(int nodes, double edgeProb, std::uint64_t seed) {
    Repository repo;
    repo.name = "random-dag-" + std::to_string(seed);
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> pick(0.0, 1.0);
    for (int i = 0; i < nodes; ++i) {
        RepositoryPackage p; p.name = PackageName{"n" + std::to_string(i)};
        // Only allow edges to strictly higher-indexed nodes so the graph is
        // acyclic by construction.
        for (int j = i + 1; j < nodes; ++j) {
            if (pick(rng) < edgeProb)
                p.depends.push_back(PackageName{"n" + std::to_string(j)});
        }
        RepositoryVersion rv; rv.version = PackageVersion{"1.0"};
        p.versions.push_back(rv); repo.packages.push_back(std::move(p));
    }
    return repo;
}

// ---------------------------------------------------------------------------
// Measurement
// ---------------------------------------------------------------------------

struct Row {
    std::string fixture;
    std::size_t packages;
    std::size_t vars;
    std::size_t clauses;
    double scanMs;
    double graphMs;
    double varsMs;
    double clausesMs;
    double solveMs;
    double mapMs;
    double totalMs;
    bool satisfiable;
    std::size_t selected;
};

Row measure(const std::string& label, const Repository& repo,
            const std::vector<std::string>& roots) {
    Row r;
    r.fixture = label;
    r.packages = repo.packages.size();

    GraphTranslator translator(repo);
    TranslatePhases phases;

    auto t0 = std::chrono::steady_clock::now();
    Problem problem = translator.translateTimed(roots, phases);
    auto t1 = std::chrono::steady_clock::now();

    DpllSolver solver;
    auto res = solver.solve(problem);
    auto t2 = std::chrono::steady_clock::now();

    // Assignment mapping phase: reconstruct the selected package set, mirroring
    // what SatResolver does after solving.
    std::size_t selected = 0;
    for (const auto& name : translator.realPackages()) {
        Variable v = problem.lookup(name);
        if (v && res.assignment.isAssigned(v) && res.assignment.get(v)) ++selected;
    }
    auto t3 = std::chrono::steady_clock::now();

    auto ms = [](auto a, auto b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };

    r.vars = problem.variableCount();
    r.clauses = problem.clauseCount();
    r.scanMs = phases.ms(phases.repositoryScan);
    r.graphMs = phases.ms(phases.graphBuild);
    r.varsMs = phases.ms(phases.variableAssign);
    r.clausesMs = phases.ms(phases.clauseEmit);
    r.solveMs = ms(t1, t2);
    r.mapMs = ms(t2, t3);
    r.totalMs = ms(t0, t3);
    r.satisfiable = res.satisfiable;
    r.selected = selected;
    return r;
}

void printTableHeader() {
    std::cout << std::left
              << std::setw(20) << "fixture"
              << std::setw(8)  << "pkgs"
              << std::setw(8)  << "vars"
              << std::setw(9)  << "clauses"
              << std::setw(9)  << "scan"
              << std::setw(9)  << "graph"
              << std::setw(8)  << "vars"
              << std::setw(9)  << "clauses"
              << std::setw(9)  << "solve"
              << std::setw(8)  << "map"
              << std::setw(9)  << "total"
              << std::setw(5)   << "sat"
              << "sel\n";
    std::cout << std::string(120, '-') << "\n";
}

void printTableRow(const Row& r) {
    std::cout << std::left
              << std::setw(20) << r.fixture
              << std::setw(8)  << r.packages
              << std::setw(8)  << r.vars
              << std::setw(9)  << r.clauses
              << std::setw(9)  << std::fixed << std::setprecision(2) << r.scanMs
              << std::setw(9)  << r.graphMs
              << std::setw(8)  << r.varsMs
              << std::setw(9)  << r.clausesMs
              << std::setw(9)  << r.solveMs
              << std::setw(8)  << r.mapMs
              << std::setw(9)  << r.totalMs
              << std::setw(5)   << (r.satisfiable ? "yes" : "no")
              << r.selected << "\n";
    std::cout.flush();
}

void printCsvHeader() {
    std::cout << "fixture,packages,vars,clauses,scan_ms,graph_ms,"
                 "variable_ms,clause_ms,solve_ms,map_ms,total_ms,"
                 "satisfiable,selected\n";
}

void printCsvRow(const Row& r) {
    std::cout << r.fixture << ","
              << r.packages << ","
              << r.vars << ","
              << r.clauses << ","
              << std::fixed << std::setprecision(4) << r.scanMs << ","
              << r.graphMs << ","
              << r.varsMs << ","
              << r.clausesMs << ","
              << r.solveMs << ","
              << r.mapMs << ","
              << r.totalMs << ","
              << (r.satisfiable ? "yes" : "no") << ","
              << r.selected << "\n";
    std::cout.flush();
}

}  // namespace

int main(int argc, char** argv) {
    bool csv = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--csv") csv = true;
    }

    struct Spec { std::string name; std::function<Repository()> gen; std::vector<std::string> roots; };
    std::vector<Spec> specs = {
        {"deep-100",      [&]{ return deepChain(100); },        {"p0"}},
        {"deep-1000",     [&]{ return deepChain(1000); },       {"p0"}},
        {"deep-10000",    [&]{ return deepChain(10000); },      {"p0"}},
        {"deep-50000",    [&]{ return deepChain(50000); },      {"p0"}},
        {"wide-d3-f20",   [&]{ return wideGraph(3, 20); },      {"root"}},
        {"wide-d4-f10",   [&]{ return wideGraph(4, 10); },      {"root"}},
        {"wide-d2-f100",  [&]{ return wideGraph(2, 100); },     {"root"}},
        {"providers-50",  [&]{ return manyProviders(50); },     {"root"}},
        {"providers-500", [&]{ return manyProviders(500); },    {"root"}},
        {"manyver-100x5", [&]{ return manyVersions(100, 5); },  {"v0"}},
        {"manyver-1000x5",[&]{ return manyVersions(1000, 5); }, {"v0"}},
        {"dense-100",     [&]{ return denseConflicts(100); },   {"c0"}},
        {"dense-500",     [&]{ return denseConflicts(500); },   {"c0"}},
        {"dag-200-0.05",  [&]{ return randomDag(200, 0.05, 42); },  {"n0"}},
        {"dag-1000-0.02", [&]{ return randomDag(1000, 0.02, 42); }, {"n0"}},
        {"dag-5000-0.01", [&]{ return randomDag(5000, 0.01, 42); }, {"n0"}},
        {"manyvirt-100x5",  [&]{ return manyVirtuals(100, 5); },    {"root"}},
        {"manyvirt-1000x10",[&]{ return manyVirtuals(1000, 10); },  {"root"}},
    };

    if (csv) {
        printCsvHeader();
    } else {
        std::cout << "SAT benchmark (DPLL baseline)\n";
        std::cout << "Times in milliseconds. Phases: scan=repository scan, "
                     "graph=adjacency build, vars=variable assignment, "
                     "clauses=clause generation, solve=SAT solving, "
                     "map=assignment mapping.\n";
        std::cout << "----------------------------------------\n";
        printTableHeader(); std::cout.flush();
    }

    for (const auto& s : specs) {
        if (const char* only = std::getenv("BENCH_ONLY")) {
            if (s.name != only) continue;
        }
        Row r = measure(s.name, s.gen(), s.roots);
        if (csv) printCsvRow(r);
        else printTableRow(r);
    }
    return 0;
}
