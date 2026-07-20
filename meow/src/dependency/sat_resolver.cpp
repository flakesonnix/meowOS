#include <meow/dependency/sat_resolver.hpp>
#include <meow/dependency/constraint.hpp>
#include <meow/dependency/version_pick.hpp>
#include <meow/sat/translate.hpp>
#include <meow/sat/solver.hpp>
#include <meow/error/error.hpp>

#include <set>
#include <unordered_map>

namespace meow::dependency {
namespace {

// After SAT returns UNSAT, collect structural diagnostics without running
// the solver again. Scans for:
//   - conflicting packages reachable from roots
//   - virtual dependency with no provider
//   - versions constraint with no satisfying version
// This is necessarily incomplete (no CDCL) but catches the most common cases.
void collectUnsatDiagnostics(
    const repository::Repository& repo,
    const std::vector<types::PackageName>& roots,
    std::vector<ResolveDiagnostic>& diags
) {
    // Build forward closure from roots (the set of packages reachable via
    // dependency edges). These are the candidates that could be selected.
    std::set<std::string> closure;
    {
        std::vector<std::string> stack;
        for (const auto& r : roots) {
            if (closure.insert(r.value).second) stack.push_back(r.value);
        }
        while (!stack.empty()) {
            auto cur = std::move(stack.back());
            stack.pop_back();
            const auto* pkg = repository::findPackage(repo, types::PackageName{cur});
            if (!pkg) continue;
            for (const auto& dep : pkg->depends) {
                if (closure.insert(dep.name.value).second)
                    stack.push_back(dep.name.value);
            }
        }
    }

    // 1. Conflict pairs within the closure
    for (const auto& name : closure) {
        const auto* pkg = repository::findPackage(repo, types::PackageName{name});
        if (!pkg) continue;
        for (const auto& conflict : pkg->conflicts) {
            if (closure.count(conflict.value)) {
                ResolveDiagnostic d;
                d.kind = ResolveDiagnostic::Kind::PackageConflict;
                d.package = types::PackageName{name};
                d.message = name + " conflicts with " + conflict.value;
                diags.push_back(std::move(d));
                return;  // one conflict is enough
            }
        }
    }

    // 2. Virtual dependency with no provider in the closure
    for (const auto& name : closure) {
        const auto* pkg = repository::findPackage(repo, types::PackageName{name});
        if (!pkg) continue;
        for (const auto& dep : pkg->depends) {
            if (repository::findPackage(repo, dep.name)) continue;
            // dep is a virtual name — check if any package provides it
            bool hasProvider = false;
            for (const auto& rp : repo.packages) {
                for (const auto& p : rp.provides) {
                    if (p.value == dep.name.value) { hasProvider = true; break; }
                }
                if (hasProvider) break;
            }
            if (!hasProvider) {
                ResolveDiagnostic d;
                d.kind = ResolveDiagnostic::Kind::MissingProvider;
                d.package = dep.name;
                d.message = "no provider for virtual dependency: " + dep.name.value;
                diags.push_back(std::move(d));
                return;
            }
        }
    }

    // 3. Version constraint with no satisfying version
    for (const auto& name : closure) {
        const auto* pkg = repository::findPackage(repo, types::PackageName{name});
        if (!pkg || pkg->depends.empty()) continue;
        for (const auto& dep : pkg->depends) {
            if (dep.constraints.empty()) continue;
            const auto* target = repository::findPackage(repo, dep.name);
            if (!target) continue;
            bool anySatisfy = false;
            const types::PackageVersion* highest = nullptr;
            for (const auto& rv : target->versions) {
                if (dependency::satisfiesConstraints(rv.version, dep.constraints)) {
                    anySatisfy = true;
                    break;
                }
                if (!highest || repository::compareVersions(rv.version, *highest) > 0)
                    highest = &rv.version;
            }
            if (!anySatisfy) {
                ResolveDiagnostic d;
                d.kind = ResolveDiagnostic::Kind::VersionConflict;
                d.package = dep.name;
                d.message = "no version of " + dep.name.value + " satisfies constraints";
                for (const auto& c : dep.constraints) {
                    if (!d.requiredVersion.empty()) d.requiredVersion += ",";
                    d.requiredVersion += c.op + c.version.value;
                }
                if (highest) d.availableVersion = highest->value;
                diags.push_back(std::move(d));
                return;
            }
        }
    }
}

}  // namespace

ResolveResult SatResolver::resolve(const repository::Repository& repo,
                                   const ResolveRequest& req) {
    ResolveResult result;

    try {
    InstallRequest ireq;
    ireq.packages = req.roots;
    ireq.includeAllOptional = req.includeAllOptional;
    ireq.selectedOptional = req.selectedOptional;
    auto roots = expandInstallRequest(repo, ireq);

    for (const auto& root : roots) {
        if (!repository::findPackage(repo, root)) {
            ResolveDiagnostic d;
            d.kind = ResolveDiagnostic::Kind::MissingPackage;
            d.package = root;
            d.message = "package not found: " + root.value;
            result.diagnostics.push_back(std::move(d));
            return result;
        }
    }

    sat::GraphTranslator translator(repo);
    std::vector<std::string> rootStrs;
    for (const auto& r : roots) rootStrs.push_back(r.value);
    sat::Problem problem = translator.translate(rootStrs);

    sat::DpllSolver defaultSolver;
    sat::Solver* solver = solver_ ? solver_ : &defaultSolver;
    auto solved = solver->solve(problem);

    if (!solved.satisfiable) {
        collectUnsatDiagnostics(repo, roots, result.diagnostics);
        if (result.diagnostics.empty()) {
            ResolveDiagnostic d;
            d.kind = ResolveDiagnostic::Kind::MissingPackage;
            d.message = "no satisfiable selection (SAT UNSAT)";
            result.diagnostics.push_back(std::move(d));
        }
        return result;
    }

    std::set<std::string> rootSet;
    for (const auto& r : roots) rootSet.insert(r.value);

    for (const auto& name : translator.realPackages()) {
        sat::Variable v = problem.lookup(name);
        if (v == 0) continue;
        if (!solved.assignment.isAssigned(v) || !solved.assignment.get(v))
            continue;

        const auto* rp = repository::findPackage(repo, types::PackageName{name});
        if (!rp) continue;

        std::optional<types::PackageVersion> ver;
        for (const auto& rv : rp->versions) {
            std::string verName = name + "@" + rv.version.value;
            sat::Variable vv = problem.lookup(verName);
            if (vv && solved.assignment.isAssigned(vv) && solved.assignment.get(vv)) {
                ver = rv.version;
                break;
            }
        }
        if (!ver) ver = detail::pickVersion(*rp);
        if (!ver) continue;

        ResolvedPackage rpkg;
        rpkg.name = types::PackageName{name};
        rpkg.version = *ver;
        rpkg.isRoot = rootSet.count(name) > 0;
        result.packages.push_back(std::move(rpkg));
    }

    result.ok = true;
    return result;
    } catch (const error::MeowError& e) {
        ResolveDiagnostic d;
        d.kind = ResolveDiagnostic::Kind::Cycle;
        d.message = e.what();
        result.diagnostics.push_back(std::move(d));
        return result;
    }
}

}  // namespace meow::dependency
