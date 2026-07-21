# Package Metadata Signing Design (next-generation)

Status: design proposal (v0.6 → v0.8). No code, no production changes.

This document proposes the signing model that closes the remaining HIGH finding
from `docs/security-audit-v0.5.md`:

> `repository.toml` is signed, but per-package manifests and version artifacts
> (which carry the artifact `sha256`) are not independently authenticated. A
> malicious mirror could replace package metadata and the matching hashes; the
> download-time checksum then validates against the attacker-chosen hash.

---

## 1. Current trust chain

### Flow

```
meow-build
   └─ builds package artifact  (name-version.pkg.tar.zst)   [UNSIGNED file]
        │
meow-repo add <artifact>
   └─ writes by-name/<sh>/<name>/package.toml
            by-name/<sh>/<name>/versions/<ver>.toml  (contains artifact url + sha256)
        │
meow-repo sign --key meow-release.pem --key-id meow-release --repo <dir>
   └─ signs ONLY repository.toml  →  repository.toml.sig   [Ed25519]
        │
client: openRepository(url)
   └─ downloadFile(repository.toml), verifyRepoSig(repository.toml, .sig)
        │  (only repository.toml is verified)
        ▼
client: resolvePackage → for each requested package:
   └─ read by-name/<sh>/<name>/package.toml      [NOT verified]
      read by-name/<sh>/<name>/versions/<ver>.toml  [NOT verified; carries sha256]
      downloadArtifact(url) → verifyChecksum(file, declared sha256)
        │   (checksum validates file against the *declared* hash)
        ▼
   install artifact
```

### Exactly where trust ends

Trust is established **only at the `repository.toml` index** via
`verifyRepoSig` (`backend_detail.hpp`). The signed index contains
`name`, `repository_id`, `mirrors`, `expires` — but **no per-package hashes**.

Everything below the index is authenticated **only transitively and weakly**:

- The `by-name/.../package.toml` and `versions/<ver>.toml` files are read with
  no signature check.
- The artifact `sha256` lives inside the **unsigned** `versions/<ver>.toml`.
- `verifyChecksum` (`resolver.cpp`) only confirms the downloaded bytes equal
  that *declared* (unsigned) hash.

So an attacker who can write the version manifest can set `sha256 = <hash of
their malicious artifact>`, and the whole chain validates. Trust ends at the
index; the package metadata is outside it.

---

## 2. Design goals (constraints the new model must satisfy)

| Goal | Why it matters |
|------|----------------|
| Keep Ed25519 | No new crypto primitive; reuses `crypto/signature.cpp`. |
| Preserve `repository_id` | Used for cache namespacing, mirror groups, identity. |
| Preserve mirror groups | `[[mirrors]]` failover must still work. |
| Preserve offline verification | A client with the trusted key must verify without network. |
| Preserve cache model where possible | `~/.cache/meow/repos/<id>/` is keyed by `repository_id`; keep it. |
| Support future HTTP mirrors | `packages.index` already drives HTTP discovery — build on it. |
| Avoid signing every file individually if unnecessary | Per-file `.sig` per package/version is heavy and slow to verify. |

---

## 3. Approaches compared

### 3.1 Per-package signatures (`package.toml.sig`, `artifact.sig`)

Each manifest and each artifact gets its own detached signature.

- **Pro:** granular; a single corrupted package doesn't taint others.
- **Con:** N signatures to generate and N to verify on every refresh; couples
  signing to the artifact bytes (re-signing an artifact re-emits `artifact.sig`);
  the `by-name` tree grows 2× in file count; no single root to pin; offline
  verification requires fetching many `.sig` files. High implementation and
  verification effort, high metadata bandwidth. Fails the "avoid signing every
  file individually" goal.

### 3.2 Signed package index (`packages.index` + `packages.index.sig`)

A single machine-readable index enumerates every package version with its
`name`, `version`, manifest hash, artifact hash, size, and dependencies. The
index is signed once; clients verify the index, then check each fetched
manifest/artifact against the index's recorded hashes.

- **Pro:** one signature, one verification, offline-capable; directly reuses
  the existing `packages.index` discovery file (already used by the HTTP
  backend); mirrors serve one extra signed file; no per-file `.sig` storm;
  cache model and `repository_id` untouched; `repository.toml` keeps its own
  signature (or is folded in). Lowest complexity/effort, highest leverage.
- **Con:** a rotated/added package requires regenerating the whole index (fine
  — it's cheap, one file). The index must be complete and canonical (stable
  ordering, stable serialization) or signatures break across tools.

### 3.3 Merkle tree / signed root hash

Build a Merkle/Hash tree over all package metadata + artifacts; sign only the
root. Clients verify the root, then a log-proof path for any package.

- **Pro:** supports incremental/partial verification and tamper-evident logs;
  strongest against selective omission.
- **Con:** significant new crypto/verification code (proof validation, canonical
  tree serialization), more complex key/cache invalidation, harder to debug,
  overkill for a single-repo trust model with no transparency-log requirement.
  High implementation effort, high migration cost.

### Comparison matrix

| Dimension | 3.1 per-pkg sig | 3.2 signed index | 3.3 Merkle |
|-----------|-----------------|------------------|------------|
| Complexity | High | **Low** | Very high |
| Security | Strong (granular) | **Strong (root-authed)** | Strongest |
| Impl effort | High | **Low–Med** | Very high |
| Verification cost | O(N) sigs | **O(1) sig + O(fetched) hashes** | O(log N) proof |
| Migration cost | High (file layout) | **Low (add 2 files)** | High (new format) |
| Offline | Yes (fetch all) | **Yes (1 file)** | Yes |
| Reuses `packages.index` | No | **Yes** | No |

---

## 4. Recommendation

**Adopt Approach 3.2 — the signed package index** — as the authoritative
authenticated metadata document, layered on top of (and eventually replacing
the index-only role of) `repository.toml`.

Rationale: it is the only approach that closes the HIGH finding with minimal
new surface, reuses the existing `packages.index` discovery path used by the
HTTP backend, keeps a single Ed25519 signature and offline verification, and
preserves the `repository_id`/cache/mirror model unchanged. Per-package
signatures (3.1) and Merkle (3.3) are stronger in narrow ways but their cost is
not justified for a trust model that already has one trusted signer per repo.

### 4.1 Exact signed data format

Two signed roots, both Ed25519 detached signatures (`signature.cpp` style):

1. **`repository.toml` + `repository.toml.sig`** (unchanged) — carries
   `repository_id`, `mirrors`, `expires`, and now a pointer to the index.
   Extended with:
   ```toml
   [signing]
   index = "packages.index"
   index_signature = "packages.index.sig"
   index_key_id = "meow-release"
   ```

2. **`packages.index` + `packages.index.sig`** — the authoritative manifest.
   Canonical, deterministic serialization. Format (stable key order, one line
   per version, sorted by `name` then `version`):

   ```
   # meow package index v1  (signed; do not edit by hand)
   P name version manifest_sha256 artifact_sha256 size_bytes dependencies
   P hello 1.1.0 <sha256 of by-name/he/hello/package.toml>
                     <sha256 of hello-1.1.0.pkg.tar.zst> 42112
                     hello>1.0.0,libfoo
   P app 1.0.0   <sha256 of by-name/ap/app/package.toml>
                     <sha256 of app-1.0.0.pkg.tar.zst> 88120 app>1.0.0,libfoo
   ...
   ```
   - `manifest_sha256` = hash of the exact `by-name/<sh>/<name>/package.toml`
     bytes the client will fetch.
   - `artifact_sha256` = hash of the exact artifact bytes (replaces/authorizes
     the `sha256` currently inside `versions/<ver>.toml`).
   - `dependencies` = comma-separated, for quick resolver pre-checks (optional,
     informational; not a trust root on its own).
   - The index is **canonical**: keys sorted, lines sorted, no floating
     whitespace — so `meow-repo` and any future tool produce byte-identical
     files and the detached signature is stable.

   `packages.index.sig` is a detached Ed25519 signature over the exact bytes of
   `packages.index`, in the same TOML-keyed form `verifyFile` already consumes
   (`algorithm`, `keyId`, `signature`).

### 4.2 Key ownership

- One **repository signing key pair** per repository (as today). The private
  key signs both `repository.toml` and `packages.index`; the public key is the
  trusted key installed in `~/.config/meow/keys/<id>.pem`.
- `keyId` in both signatures references the same trusted key id (e.g.
  `meow-release`). `verifyFile` already selects the trusted key by `keyId` and
  cryptographically verifies — no change needed there.
- No per-package keys; the single repo key is the root of trust, exactly as
  today. This keeps the key story simple and matches the existing model.

### 4.3 Repository generation flow (`meow-repo`)

1. `meow-repo add <artifact>` — writes `by-name/...` manifests and the artifact
   into the repo (unchanged).
2. `meow-repo sync` / `add` also updates an in-memory view of all packages.
3. `meow-repo sign --key K --key-id ID`:
   - Recomputes `packages.index` deterministically from the `by-name` tree
     (manifest hash + artifact hash + size + deps per version).
   - Writes `packages.index`.
   - Signs it → `packages.index.sig`.
   - Re-signs `repository.toml` (now carrying the `[signing]` pointer).
   - Optionally signs artifacts themselves too (see §4.7) — but the index hash
     is sufficient; artifact signing is optional and off by default.

   Because the index is one file, re-signing on every publish is O(1) work.

### 4.4 Client verification flow

On `openRepository` / `refresh`:

1. Fetch + verify `repository.toml` (existing `verifyRepoSig`). On strict mode
   this is already enforced.
2. Read the `[signing]` pointer; fetch `packages.index` and
   `packages.index.sig`.
3. Verify `packages.index.sig` against the trusted key id (same `verifyFile`).
   If `requireRepositorySignature` (strict) is set and the index or its
   signature is missing/invalid → `InvalidSignature` (extends the existing
   policy to the index).
4. Load the index into memory: a `name→{version→{manifest_sha256,
   artifact_sha256, size}}` map.
5. When resolving a package:
   - Fetch `by-name/<sh>/<name>/package.toml`; **verify its sha256 equals the
     index's `manifest_sha256`** (replaces the current "trust by fetch").
   - Fetch `versions/<ver>.toml`; ignore its inline `sha256` for trust and use
     the **index's `artifact_sha256`** as the authoritative artifact hash.
   - Download the artifact; `verifyChecksum(file, index.artifact_sha256)`.
6. Result: a malicious mirror can no longer substitute a different manifest or
   artifact, because both are now bound to the signed index.

   Offline verification works: the trusted key + the two signed files are
   sufficient; no extra network round-trips beyond fetching them.

### 4.5 Mirror behavior

- Mirrors replicate the entire repo directory, including `packages.index` and
  `packages.index.sig`. Failover logic (`failover.cpp`) is unchanged: transport
  errors fall over; trust errors (`InvalidSignature`, `RepositoryExpired`,
  missing index under strict mode) **stop** the chain. A mirror that serves a
  tampered index fails verification on every mirror equally — there is no
  "good" mirror with different (attacker) data, because all legitimate mirrors
  carry the same signed index.
- Mirror groups (`[[mirrors]]`) and `repository_id` are untouched; the cache is
  still namespaced by `repository_id`, now also caching the verified index.

### 4.6 Cache invalidation rules

- Keep `~/.cache/meow/repos/<repository_id>/` as today.
- Add `packages.index` and `packages.index.sig` to `refreshRepoCache`
  (alongside `repository.toml`/`.sig`).
- Invalidation: re-fetch the index when (a) `repository.toml` `expires` passes,
  (b) `last_checked` exceeds the refresh window, or (c) the local index hash
  differs from a re-fetched `packages.index` and the new signature still
  verifies (normal rotation). If the re-fetched index fails signature
  verification, keep the previously verified index and mark the source
  untrusted (failover/stop), never silently downgrade to the unsigned
  `by-name` metadata.

### 4.7 Optional: artifact self-signing (out of initial scope)

The index already authorizes the artifact hash, so signing each
`*.pkg.tar.zst` individually is unnecessary. If desired later, `artifact.sig`
can be added for defense-in-depth (e.g., to validate an artifact fetched from a
CDN whose URL differs from the repo host) without changing the index model.
Default off.

---

## 5. Migration plan

### v0.6 — current model (baseline)
- `repository.toml` signed; `by-name` manifests + artifact hashes **unsigned**.
- Strict mode (`requireRepositorySignature`) enforces the index signature only.
- This is the state the audit reviewed.

### v0.7 — new signing model optional (additive, non-breaking)
- `meow-repo` gains index generation: emits `packages.index` +
  `packages.index.sig` and the `[signing]` pointer in `repository.toml`.
- **Backward compatible:** clients that don't understand the index simply
  ignore it and behave as in v0.6 (still vulnerable, but not broken).
- **Opt-in verification:** a client that finds a valid `packages.index.sig`
  verifies package metadata/artifacts against it. If `packages.index` is
  absent, the client falls back to the v0.6 behavior (warn, as today) unless
  strict mode is set — in which case a missing index becomes `InvalidSignature`
  (closing the gap for repos that publish an index).
- Repos that don't publish an index are treated exactly as v0.6 (no regression
  for existing mirrors).

### v0.8 — strict mode possible (enforced)
- `requireRepositorySignature = true` (or `MEOW_REQUIRE_SIGNATURE=1`) now
  requires a **valid `packages.index.sig`** in addition to
  `repository.toml.sig`. A repo without a signed index is rejected.
- The unsigned `by-name` metadata path is no longer trusted; all package
  metadata and artifact hashes are bound to the signed index. The HIGH finding
  is fully closed.
- Legacy repos (no index) must be re-signed with `meow-repo sign` to opt into
  v0.8 strict mode; non-strict clients continue to interoperate.

---

## 6. Open questions / risks

- **Canonical index serialization** must be nailed down (field order, line
  sort, hash algorithm, newline handling) so signatures are stable across
  `meow-repo` versions and across OS line-ending differences. Recommend a
  fixed format version header (`# meow package index v1`) and a test that
  re-signing identical input yields identical bytes.
- **Index size** for very large repos: still one file, fetched once per refresh;
  acceptable. If it grows unwieldy, a future split into per-shard signed index
  chunks is compatible with the same verification model.
- **`manifest_sha256` vs inline manifest drift:** the client must verify the
  fetched manifest hash *before* parsing it; if it differs, reject (do not log
  and continue).
- **Key rotation:** rotating the repo key requires re-signing both
  `repository.toml` and `packages.index`; document as part of `meow-repo sign`.
