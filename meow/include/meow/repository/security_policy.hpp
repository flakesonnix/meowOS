#ifndef MEOWOS_REPOSITORY_SECURITY_POLICY_H
#define MEOWOS_REPOSITORY_SECURITY_POLICY_H

namespace meow::repository {

// Process-wide repository security policy. The CLI sets this once from the
// loaded configuration before any repository is opened; the backends (which are
// constructed deep inside createBackend(), far from the config) consult it when
// verifying signatures. This keeps the trust decision configurable without
// threading a flag through every backend constructor and refresh call site.
struct SecurityPolicy {
    // When true, a repository without a valid signature is rejected:
    //   - no `.sig` file present            -> hard error
    //   - signature present but empty keyId  -> hard error
    //   - signature present but invalid      -> hard error (already enforced)
    bool requireRepositorySignature = false;

    // v0.7 placeholder. When true (future), a repository must ship a signed
    // package index (`packages.toml` + `packages.toml.sig`) and the client
    // verifies every loaded package manifest/artifact hash against it. Default
    // false keeps current behavior (unsigned per-package metadata accepted).
    // Not yet wired into any production path — see
    // docs/package-index-signing-implementation-plan.md.
    bool requirePackageIndex = false;
};

// Set the global policy. Call once at startup from config.
void setSecurityPolicy(const SecurityPolicy& policy);

// Read the current global policy (default: nothing required).
const SecurityPolicy& securityPolicy();

}  // namespace meow::repository

#endif  // MEOWOS_REPOSITORY_SECURITY_POLICY_H
