#include <meow/dependency/sat_resolver.hpp>
#include <meow/dependency/version_pick.hpp>
#include <meow/sat/translate.hpp>
#include <meow/sat/solver.hpp>
#include <meow/error/error.hpp>

#include <set>

namespace meow::dependency {

ResolveResult SatResolver::resolve(const repository::Repository& repo,
                                   const ResolveRequest& req) {
    ResolveResult result;

    try {
    // Optional promotion reuses the existing expansion so the SAT backend and
    // legacy backend agree on which roots enter resolution.
    InstallRequest ireq;
    ireq.packages = req.roots;
    ireq.includeAllOptional = req.includeAllOptional;
    ireq.selectedOptional = req.selectedOptional;
    auto roots = expandInstallRequest(repo, ireq);

    // Root existence check (parity with LegacyResolver). Any root that is not
    // findable in the repository — whether by exact name or provides — is a
    // hard error. The SAT translator would happily create a variable for it
    // and produce a vacuous satisfiable assignment, which would silently
    // install nothing for that root.
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
        ResolveDiagnostic d;
        d.kind = ResolveDiagnostic::Kind::MissingPackage;
        d.message = "no satisfiable selection (SAT UNSAT)";
        result.diagnostics.push_back(std::move(d));
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
        auto ver = detail::pickVersion(*rp);
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
        // Expansion/closure failures (e.g. cycles) become diagnostics so the
        // IResolver contract stays exception-free and parity with the legacy
        // backend is comparable.
        ResolveDiagnostic d;
        d.kind = ResolveDiagnostic::Kind::Cycle;
        d.message = e.what();
        result.diagnostics.push_back(std::move(d));
        return result;
    }
}

}  // namespace meow::dependency
