#include <meow/sat/translate.hpp>

#include <map>
#include <unordered_map>
#include <vector>

#include <meow/repository/repository.hpp>

namespace meow::sat {

namespace {
    using Clock = std::chrono::steady_clock;
    std::uint64_t us(Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
    }
}  // namespace

Problem GraphTranslator::translateTimed(const std::vector<std::string>& roots,
                                        TranslatePhases& phases) {
    auto tStart = Clock::now();
    Problem problem;

    // --- Stage 0: repositoryScan — one pass to build the name->index map so
    // every later lookup is O(1). No edges recorded yet.
    std::map<std::string, std::size_t> indexOf;
    for (std::size_t i = 0; i < repo_.packages.size(); ++i)
        indexOf[repo_.packages[i].name.value] = i;
    auto tScan = Clock::now();
    phases.repositoryScan = us(tStart, tScan);

    // --- Stage 1: buildGraph — build forward/reverse adjacency,
    // conflict lists, and virtualDependents (who depends on virtual names).
    // Reverse deps use indexOf so each edge is O(1).
    // Dependencies on virtual names (not in indexOf) go into virtualDependents.
    std::vector<std::vector<std::string>> forward(repo_.packages.size());
    std::vector<std::vector<std::string>> reverse(repo_.packages.size());
    std::vector<std::vector<std::string>> conflicts(repo_.packages.size());
    std::map<std::string, std::vector<std::string>> virtualDependents;
    for (std::size_t i = 0; i < repo_.packages.size(); ++i) {
        const auto& pkg = repo_.packages[i];
        for (const auto& dep : pkg.depends) {
            forward[i].push_back(dep.value);
            auto it = indexOf.find(dep.value);
            if (it != indexOf.end()) {
                reverse[it->second].push_back(pkg.name.value);
            } else {
                // Dependency is a virtual name (not a real package).
                // Track who depends on it for the virtual reverse clause.
                virtualDependents[dep.value].push_back(pkg.name.value);
            }
        }
        for (const auto& conf : pkg.conflicts) {
            if (indexOf.find(conf.value) != indexOf.end())
                conflicts[i].push_back(conf.value);
        }
    }
    auto tGraph = Clock::now();
    phases.graphBuild = us(tScan, tGraph);

    // --- Stage 2: assignVariables — declare a variable per package and per
    // virtual provide name. Deterministic (repo order), so output is stable.
    for (const auto& pkg : repo_.packages) {
        real_.push_back(pkg.name.value);
        problem.declare(pkg.name.value);
        for (const auto& prov : pkg.provides) problem.declare(prov.value);
    }
    auto tVars = Clock::now();
    phases.variableAssign = us(tGraph, tVars);

    // --- Stage 3: buildProviderMap — collect (virtual -> [provider var ids]).
    // Scanned after variables are declared so every name has a variable id.
    std::map<std::string, std::vector<Variable>> providerOf;
    for (const auto& pkg : repo_.packages) {
        if (pkg.provides.empty()) continue;
        Variable prov = problem.lookup(pkg.name.value);
        for (const auto& v : pkg.provides) {
            Variable virt = problem.lookup(v.value);
            if (prov && virt) providerOf[v.value].push_back(prov);
        }
    }

    // --- Stage 4: emitClauses — forward implications, conflicts, provider
    // clauses (virtual-needs-provider + provider-satisfies-virtual), virtual
    // reverse implications, package reverse implications, root unit clauses.
    std::unordered_map<std::string, bool> rootSet;
    for (const auto& r : roots) rootSet[r] = true;

    // 4a: Forward implications + conflicts (existing)
    for (std::size_t i = 0; i < repo_.packages.size(); ++i) {
        Variable a = problem.lookup(repo_.packages[i].name.value);
        for (const auto& dep : forward[i]) {
            Variable b = problem.declare(dep);
            problem.add(Clause::implies(a, b));
        }
        for (const auto& conf : conflicts[i]) {
            Variable c = problem.lookup(conf);
            problem.add(Clause::conflict(a, c));
        }
    }

    // 4b: Provider clauses. For each virtual V with providers P1..Pn:
    //   - Provider satisfies virtual: (¬Pi ∨ V) for each Pi
    //   - Virtual needs at least one provider: (¬V ∨ P1 ∨ ... ∨ Pn)
    //
    // (¬Pi ∨ V) also prevents spurious provider selection: combined with the
    // virtual reverse clause (4c), Pi can only be true if V's consumer is true.
    for (const auto& [virtName, provVars] : providerOf) {
        Variable virt = problem.lookup(virtName);
        if (!virt || provVars.empty()) continue;
        for (Variable pv : provVars) {
            problem.add(Clause::implies(pv, virt));
        }
        std::vector<Literal> req{virt};
        for (auto& pv : provVars) req.push_back(pv);
        req[0] = -virt;
        problem.add(Clause{std::move(req)});
    }

    // 4c: Virtual reverse clauses. For each virtual V with dependents C1..Cn:
    //   (¬V ∨ C1 ∨ ... ∨ Cn)
    // This prevents V from being true unless a consumer needs it.
    // If V has no dependents, no clause is emitted (the forbid for its
    // providers' packages in 4d handles the no-consumer case).
    // Dependencies that are virtual names (not in indexOf) were tracked in
    // virtualDependents during stage 1.
    for (const auto& [virtName, consumers] : virtualDependents) {
        Variable virt = problem.lookup(virtName);
        if (!virt || consumers.empty()) continue;
        std::vector<Literal> clause{virt};
        for (const auto& c : consumers) clause.push_back(problem.lookup(c));
        clause[0] = -virt;
        problem.add(Clause{std::move(clause)});
    }

    // 4d: Package reverse clauses. A package with no named dependents is
    // forbidden unless it is a root or provides a virtual that IS needed
    // (has consumers tracked in virtualDependents). The provider encoding
    // (4b) + virtual reverse clause (4c) handle the dependency chain.
    for (std::size_t i = 0; i < repo_.packages.size(); ++i) {
        const auto& pkg = repo_.packages[i];
        if (rootSet.count(pkg.name.value)) continue;
        Variable b = problem.lookup(pkg.name.value);
        const auto& depsOn = reverse[i];
        if (depsOn.empty()) {
            // Check if this package provides a virtual that someone depends on.
            // If so, skip the forbid — the provider encoding controls selection.
            bool providesNeeded = false;
            for (const auto& prov : pkg.provides) {
                auto it = virtualDependents.find(prov.value);
                if (it != virtualDependents.end() && !it->second.empty()) {
                    providesNeeded = true;
                    break;
                }
            }
            if (!providesNeeded) {
                problem.add(Clause{{-b}});
            }
        } else {
            std::vector<Literal> clause{-b};
            for (const auto& d : depsOn) clause.push_back(problem.lookup(d));
            problem.add(Clause{std::move(clause)});
        }
    }

    // 4e: Root unit clauses
    for (const auto& root : roots) {
        Variable r = problem.declare(root);
        problem.add(Clause{{r}});
    }
    auto tCnf = Clock::now();
    phases.clauseEmit = us(tVars, tCnf);
    phases.total = us(tStart, tCnf);

    return problem;
}

}  // namespace meow::sat
