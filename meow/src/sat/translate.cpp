#include <meow/sat/translate.hpp>

#include <algorithm>
#include <map>
#include <unordered_map>
#include <vector>

#include <meow/dependency/constraint.hpp>
#include <meow/repository/repository.hpp>
#include <meow/repository/version.hpp>

namespace meow::sat {

namespace {
    using Clock = std::chrono::steady_clock;
    std::uint64_t us(Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
    }

    struct VersionVarInfo {
        std::vector<Variable> vars;
        bool initialized = false;
    };

    struct ProviderEntry {
        std::string name;
        Variable var;
    };

    const repository::RepositoryPackage* findPkg(
        const repository::Repository& repo,
        const std::string& name) {
        for (const auto& p : repo.packages) {
            if (p.name.value == name) return &p;
        }
        return nullptr;
    }
}  // namespace

Problem GraphTranslator::translateTimed(const std::vector<std::string>& roots,
                                        TranslatePhases& phases) {
    auto tStart = Clock::now();
    Problem problem;

    // --- Stage 0: repositoryScan
    std::map<std::string, std::size_t> indexOf;
    for (std::size_t i = 0; i < repo_.packages.size(); ++i)
        indexOf[repo_.packages[i].name.value] = i;
    auto tScan = Clock::now();
    phases.repositoryScan = us(tStart, tScan);

    // --- Stage 1: buildGraph
    std::vector<std::vector<std::string>> forward(repo_.packages.size());
    std::vector<std::vector<std::string>> reverse(repo_.packages.size());
    std::vector<std::vector<std::vector<types::VersionConstraint>>> edgeConstraints(repo_.packages.size());
    std::vector<std::vector<std::string>> conflicts(repo_.packages.size());
    std::map<std::string, std::vector<std::string>> virtualDependents;
    for (std::size_t i = 0; i < repo_.packages.size(); ++i) {
        const auto& pkg = repo_.packages[i];
        for (const auto& dep : pkg.depends) {
            forward[i].push_back(dep.name.value);
            edgeConstraints[i].push_back(dep.constraints);
            auto it = indexOf.find(dep.name.value);
            if (it != indexOf.end()) {
                reverse[it->second].push_back(pkg.name.value);
            } else {
                virtualDependents[dep.name.value].push_back(pkg.name.value);
            }
        }
        for (const auto& conf : pkg.conflicts) {
            if (indexOf.find(conf.value) != indexOf.end())
                conflicts[i].push_back(conf.value);
        }
    }
    auto tGraph = Clock::now();
    phases.graphBuild = us(tScan, tGraph);

    // --- Stage 2: assignVariables
    for (const auto& pkg : repo_.packages) {
        real_.push_back(pkg.name.value);
        problem.declare(pkg.name.value);
        for (const auto& prov : pkg.provides) problem.declare(prov.value);
    }
    auto tVars = Clock::now();
    phases.variableAssign = us(tGraph, tVars);

    // --- Stage 3: buildProviderMap with deterministic ordering.
    // Build an O(1) name→package lookup for the sort comparator.
    std::map<std::string, const repository::RepositoryPackage*> pkgByName;
    for (const auto& pkg : repo_.packages)
        pkgByName[pkg.name.value] = &pkg;

    // Sort providers by (version desc, name asc) so the DPLL always
    // picks the same provider for a given virtual.
    std::map<std::string, std::vector<ProviderEntry>> providerOf;
    for (const auto& pkg : repo_.packages) {
        if (pkg.provides.empty()) continue;
        Variable prov = problem.lookup(pkg.name.value);
        for (const auto& v : pkg.provides) {
            Variable virt = problem.lookup(v.value);
            if (prov && virt) {
                providerOf[v.value].push_back({pkg.name.value, prov});
            }
        }
    }
    for (auto& [virtName, entries] : providerOf) {
        std::sort(entries.begin(), entries.end(),
            [&](const ProviderEntry& a, const ProviderEntry& b) {
                auto* pkgA = pkgByName[a.name];
                auto* pkgB = pkgByName[b.name];
                int cmp = 0;
                if (pkgA && pkgB) {
                    auto* verA = repository::latestVersion(*pkgA);
                    auto* verB = repository::latestVersion(*pkgB);
                    if (verA && verB)
                        cmp = repository::compareVersions(*verB, *verA);
                }
                if (cmp == 0) cmp = a.name.compare(b.name);
                return cmp < 0;
            });
    }

    // --- Stage 4: emitClauses
    std::unordered_map<std::string, bool> rootSet;
    for (const auto& r : roots) rootSet[r] = true;
    std::map<std::string, VersionVarInfo> versionInfo;

    auto ensureVersionVars = [&](const std::string& pkgName) {
        auto& info = versionInfo[pkgName];
        if (info.initialized) return;
        info.initialized = true;
        const auto* pkg = findPkg(repo_, pkgName);
        if (!pkg || pkg->versions.size() <= 1) return;
        Variable pkgVar = problem.lookup(pkgName);
        if (!pkgVar) return;
        std::vector<Literal> versionOr;
        versionOr.push_back(pkgVar);
        for (std::size_t vi = 0; vi < pkg->versions.size(); ++vi) {
            const auto& ver = pkg->versions[vi].version;
            std::string verName = pkgName + "@" + ver.value;
            Variable vv = problem.declare(verName);
            info.vars.push_back(vv);
            problem.add(Clause::implies(vv, pkgVar));
            versionOr.push_back(vv);
        }
        versionOr[0] = -pkgVar;
        problem.add(Clause{std::move(versionOr)});
        for (std::size_t i = 0; i < info.vars.size(); ++i) {
            for (std::size_t j = i + 1; j < info.vars.size(); ++j) {
                problem.add(Clause::conflict(info.vars[i], info.vars[j]));
            }
        }
    };

    // 4a: Forward implications + conflicts + version constraints
    for (std::size_t i = 0; i < repo_.packages.size(); ++i) {
        Variable a = problem.lookup(repo_.packages[i].name.value);
        for (std::size_t ei = 0; ei < forward[i].size(); ++ei) {
            const auto& dep = forward[i][ei];
            Variable b = problem.declare(dep);
            problem.add(Clause::implies(a, b));
            const auto& constraints = edgeConstraints[i][ei];
            if (!constraints.empty() && a && b) {
                const auto* targetPkg = findPkg(repo_, dep);
                if (!targetPkg || targetPkg->versions.empty()) continue;
                // Count how many versions satisfy all constraints.
                std::size_t satisfyCount = 0;
                for (const auto& rv : targetPkg->versions) {
                    if (dependency::satisfiesConstraints(rv.version, constraints))
                        ++satisfyCount;
                }
                if (satisfyCount == 0) {
                    // No version satisfies → A can never be selected.
                    problem.add(Clause{{-a}});
                } else if (satisfyCount < targetPkg->versions.size()) {
                    // Some but not all versions satisfy → restrict which versions
                    // are allowed when A is selected.
                    if (targetPkg->versions.size() > 1) ensureVersionVars(dep);
                    auto& info = versionInfo[dep];
                    std::vector<Literal> clause;
                    clause.push_back(-a);
                    for (std::size_t vi = 0; vi < targetPkg->versions.size(); ++vi) {
                        if (dependency::satisfiesConstraints(
                                targetPkg->versions[vi].version, constraints)) {
                            if (info.initialized && vi < info.vars.size())
                                clause.push_back(info.vars[vi]);
                            else
                                clause.push_back(b);
                        }
                    }
                    problem.add(Clause{std::move(clause)});
                }
                // If all versions satisfy, the basic (¬A ∨ B) is sufficient.
            }
        }
        for (const auto& conf : conflicts[i]) {
            Variable c = problem.lookup(conf);
            problem.add(Clause::conflict(a, c));
        }
    }

    // 4b: Provider clauses (deterministic order)
    for (const auto& [virtName, entries] : providerOf) {
        Variable virt = problem.lookup(virtName);
        if (!virt || entries.empty()) continue;
        for (const auto& e : entries) {
            problem.add(Clause::implies(e.var, virt));
        }
        std::vector<Literal> req{virt};
        for (const auto& e : entries) req.push_back(e.var);
        req[0] = -virt;
        problem.add(Clause{std::move(req)});
    }

    // 4c: Virtual reverse clauses
    for (const auto& [virtName, consumers] : virtualDependents) {
        Variable virt = problem.lookup(virtName);
        if (!virt || consumers.empty()) continue;
        std::vector<Literal> clause{virt};
        for (const auto& c : consumers) clause.push_back(problem.lookup(c));
        clause[0] = -virt;
        problem.add(Clause{std::move(clause)});
    }

    // 4d: Package reverse clauses
    for (std::size_t i = 0; i < repo_.packages.size(); ++i) {
        const auto& pkg = repo_.packages[i];
        if (rootSet.count(pkg.name.value)) continue;
        Variable b = problem.lookup(pkg.name.value);
        const auto& depsOn = reverse[i];
        if (depsOn.empty()) {
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
