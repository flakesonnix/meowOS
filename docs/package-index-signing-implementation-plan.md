# Signed Package Index — Implementation Plan (v0.7 groundwork)

Status: **pre-implementation plan + isolated boundary scaffolding only.** No
production path is wired to the index yet. The security hardening milestones
(repository signature enforcement, hardened extraction, install locking, in-process
checksum) are complete. HIGH #1 — *package manifests and the artifact `sha256`
they carry are not covered by the repository signature* — remains open and is the
target of this plan.

Design source of truth: `docs/package-signing-design.md`.

---

## 1. Current repository metadata flow (as reviewed)

1. **Generation** — `meow-repo` (`meow/src/repo-builder/repo_builder.cpp`):
   - `repoAdd` writes `by-name/<shard>/<pkg>/package.toml` (parsed metadata)
     and `versions/<ver>.toml` (carries `artifact.sha256`), copies the archive
     to `packages/`.
   - `repoSync` writes `repository.toml` (identity, mirrors, expiry).
   - `repoSigUpdate` signs `repository.toml` → `repository.toml.sig` via
     `crypto::signFile`.
   - **No `packages.toml` is produced today.**
2. **Backend loading** — `meow/src/repository/backend_detail.hpp` +
   `http_backend.cpp` / `backend.cpp`:
   - `verifyRepoSig` checks `repository.toml`/`.sig` against the trusted key,
     honoring `securityPolicy().requireRepositorySignature`.
   - `parsePackageManifest` / `parseVersionManifest` load the unsigned
     `by-name/...` files directly — **the hash here is trusted only by
     transport.**
3. **Signature verification path** — `meow/src/crypto/signature.cpp`:
   - `verifyFile`, `loadSignature`, `loadTrustedKey`, `computeSha256` exist and
     are reused as-is for the new index.

---

## 2. Exact files that need changes (for the actual v0.7 migration)

| File | Change |
|------|--------|
| `meow/src/repo-builder/repo_builder.cpp` | `repoAdd` computes `manifest_hash` + `artifact_hash` (sha256) per version; `repoSync`/`repoSigUpdate` emits + signs `packages.toml` → `packages.toml.sig` with the same key. |
| `meow/include/meow/repo-builder/repo_builder.hpp` | Add `signKey`/`signKeyId` plumbing for the index signature (reuse existing `RepoBuildOptions`). |
| `meow/src/repository/backend_detail.hpp` | After `verifyRepoSig`, fetch + verify `packages.toml`/`.sig`; on each `parsePackageManifest`/`parseVersionManifest`, recompute the canonical manifest hash and compare to the signed entry (trust failure → `InvalidManifest`). |
| `meow/src/repository/http_backend.cpp` | Download + verify the index like the filesystem backend; enumerate via the index when present. |
| `meow/src/repository/security_policy.hpp` | Promote `requirePackageIndex` from placeholder to an enforced flag (flip default remains `false` for compat). |
| `meow/src/download/downloader.cpp` | `fetchArtifact` takes the **authoritative** `artifact_hash` from the signed index rather than the unsigned version manifest. |
| `docs/security.md` | Document the signed index as the closure of HIGH #1. |

**Not changed:** resolver/SAT, dependency logic, install transaction behavior,
cache keying, `repository.toml` format, the Ed25519 crypto path.

---

## 3. Proposed data flow (v0.7)

```
meow-repo
  repoAdd  -> by-name/.../package.toml, versions/<ver>.toml, packages/*.pkg
  repoSync -> repository.toml (+ repository.toml.sig)
  (new)    -> packages.toml        (manifest_hash, artifact_hash, size, deps)
            -> packages.toml.sig    (Ed25519, same trusted key)

meow client (openRepository)
  verify repository.toml/.sig        (unchanged)
  verify packages.toml/.sig         (NEW, same key)
  for each by-name/.../package.toml  -> recompute manifest_hash, compare to index
  for each artifact download         -> verify sha256 against index.artifact_hash

failover: a bad index sig / manifest-hash mismatch is a trust failure ->
  non-failover (same policy as repository.toml today).
```

The authoritative artifact hash moves from the unsigned version manifest into the
signed index; the version manifest may keep a redundant hash but it is no longer
trusted on its own.

---

## 4. Migration steps

1. **v0.7 (this plan's target series) — index optional.**
   - `meow-repo` emits `packages.toml` + `packages.toml.sig` (additive).
   - Client verifies the index when present; when absent, falls back to current
     transport-trust (warn, like unsigned `repository.toml` today).
   - `requirePackageIndex` defaults to `false`; operators opt in.
2. **v0.8 — strict mode possible.**
   - With `requirePackageIndex = true`, a missing/invalid index is a hard error,
     mirroring `requireRepositorySignature`.
   - Repositories may bump `repository.toml` `format_version` to declare the
     signed-index contract.
3. **v1.0 — default strict** (only after ecosystem adoption).

---

## 5. Backwards compatibility strategy

- `packages.toml`/`.sig` are **additive files**; v0.6 repos without them load
  unchanged on v0.7 clients (compatibility mode).
- v0.6 clients ignore the new files entirely.
- The identity index (`repository.toml`) and `repository_id` / mirror-group /
  cache model are untouched, so offline verification, HTTP mirrors, and the
  cache layout all keep working.
- One signature key hierarchy (existing Ed25519 trusted-key store).

---

## 6. Test plan (v0.7)

- unit: parse `packages.toml` (canonical fields, deps, size).
- unit: `verifyPackageIndex` accepts a valid sig, rejects missing/mismatched sig
  with `InvalidSignature` (boundary test already scaffolds this).
- integration (`test/integration/sections/`):
  - signed-index happy path installs and verifies artifact against the signed hash;
  - tampered `package.toml` (manifest hash mismatch) → `InvalidManifest`;
  - tampered artifact (hash mismatch) → `ChecksumMismatch`;
  - missing index under `requirePackageIndex=true` → hard error;
  - unsigned index under default → warn + continue (compat).
- regression: existing archive/security/signature suites remain green.

---

## 7. What shipped in THIS commit (groundwork only)

- `meow/include/meow/repository/security_policy.hpp`: added the
  `requirePackageIndex` placeholder flag (default `false`, no behavior change).
- `meow/include/meow/repository/package_index.hpp`: placeholder
  `PackageIndex` / `PackageIndexEntry` types + `parsePackageIndex`,
  `verifyPackageIndex`, `acceptUnsignedPackageIndex` — **header-only, not wired
  into any backend or config default.**
- `test/unit/package_index_test.cpp`: boundary tests for unsigned-index
  acceptance and invalid-signature handling design.
- This document.

No resolver/SAT, dependency, install-transaction, or default-signature behavior
was changed. The new code is inert until v0.7 wires it in.
