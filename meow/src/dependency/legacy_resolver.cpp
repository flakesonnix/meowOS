#include <meow/dependency/legacy_resolver.hpp>
#include <meow/dependency/version_pick.hpp>
#include <meow/repository/resolver.hpp>
#include <meow/repository/version.hpp>
#include <meow/error/error.hpp>

#include <set>

namespace meow::dependency {

ResolveResult LegacyResolver::resolve(const repository::Repository& repo,
                                      const ResolveRequest& req) {
    ResolveResult result;

    try {
        InstallRequest ireq;
        ireq.packages = req.roots;
        ireq.includeAllOptional = req.includeAllOptional;
        ireq.selectedOptional = req.selectedOptional;

        auto roots = expandInstallRequest(repo, ireq);

        // Compute the dependency closure using repository metadata only (no
        // downloads). Each requested root plus its required subtree is expanded.
        std::set<std::string> seen;
        std::vector<types::PackageName> ordered;
        for (const auto& root : roots) {
            if (!repository::findPackage(repo, root)) {
                ResolveDiagnostic d;
                d.kind = ResolveDiagnostic::Kind::MissingPackage;
                d.package = root;
                d.message = "package not found: " + root.value;
                result.diagnostics.push_back(std::move(d));
                return result;
            }
            if (seen.insert(root.value).second) ordered.push_back(root);
            for (const auto& n : repository::resolveDependencyNames(repo, root)) {
                if (seen.insert(n.value).second) ordered.push_back(n);
            }
        }

        std::set<std::string> rootSet;
        for (const auto& r : roots) rootSet.insert(r.value);

        for (const auto& name : ordered) {
            const auto* rp = repository::findPackage(repo, name);
            if (!rp) continue;
            auto ver = detail::pickVersion(*rp);
            if (!ver) continue;
            ResolvedPackage rpkg;
            rpkg.name = name;
            rpkg.version = *ver;
            rpkg.isRoot = rootSet.count(name.value) > 0;
            result.packages.push_back(std::move(rpkg));
        }

        result.ok = true;
        return result;
    } catch (const error::MeowError& e) {
        // Closure failures (e.g. dependency cycles, missing packages) become
        // diagnostics so the IResolver contract stays exception-free and parity
        // with the SAT backend (which reports UNSAT) is comparable.
        ResolveDiagnostic d;
        d.kind = ResolveDiagnostic::Kind::Cycle;
        d.message = e.what();
        result.diagnostics.push_back(std::move(d));
        return result;
    }
}

}  // namespace meow::dependency
