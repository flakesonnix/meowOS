# meowOS Security Audit — v0.5

Scope: pre-feature-expansion review of the security-critical paths in the
package manager (`meow`), repository server (`meow-server`), builder
(`meow-build`/`meow-repo`), and the integration/hook machinery. File/line
references point at the current tree.

> **Update (v0.5 hardening pass).** The three HIGH findings actionable without a
> repository-format change have been fixed; see **§7 Remediation status**. The
> remaining HIGH (unsigned per-package manifests) requires a signed-manifest
> format and is documented as a known trust boundary in §1.1 until the format
> lands. The original review text below is preserved for context.

Audited areas:

1. Repository trust chain (signature, identity, expiry, mirror failover, cache isolation)
2. Hook execution security (environment, filesystem, cwd, network, timeout)
3. Package installation security (archive extraction, traversal, ownership, transactions, rollback)
4. Download / checksum handling

Severity scale: **Critical > High > Medium > Low**. "Recommended fix" items
are proposals for the next hardening pass; none are implemented here.

---

## 1. Repository trust chain

### 1.1 Signature verification

What is verified:

- `verifyRepoSig` (`meow/src/repository/backend_detail.hpp:139`) verifies
  **only `repository.toml`** against `repository.toml.sig` using
  `crypto::verifyFile` (`meow/src/crypto/signature.cpp:30`).
- `verifyFile` loads the trusted key named by `sig.keyId`
  (`backend_detail.hpp:161` → `keystore.cpp:16`) and runs an Ed25519
  `EVP_DigestVerify`. The `mdtype=nullptr` in `EVP_DigestVerifyInit`
  (`signature.cpp:72`) is **correct** for Ed25519 (pure signature, no pre-hash).
- Failover is trust-aware: trust/metadata failures stop the mirror chain;
  only transport errors fall over (`repository/failover.cpp:7`,
  `status.cpp:12`). This is well designed.

Gaps:

- **(HIGH) Only the index is signed.** Per-package manifests
  (`by-name/<name>/package.toml`) and version artifacts
  (`by-name/<name>/versions/<ver>.toml`, which contain the artifact `sha256`)
  are loaded **without any signature** (`backend_detail.hpp:174`,
  `parseVersionManifest` at `:232`). The signed `repository.toml` is an index,
  not a signed manifest of every package's hash. A network-position attacker
  (or a malicious mirror that somehow passes failover — see 1.2) can rewrite
  package metadata and the matching `sha256` in the version manifest; the
  download-time checksum check (`resolver.cpp:29`) then validates against the
  *attacker-chosen* hash and passes. The artifact hash in the trust chain is
  therefore only as trustworthy as the transport, not the signature.
- **(HIGH — FIXED)** ~~Unsigned repositories are accepted.~~ A new
  `[security] require_repository_signature` config flag (default `false`,
  preserving current behavior) now makes `verifyRepoSig` fail hard on a missing
  `.sig` when enabled. Enforced through a process-wide `SecurityPolicy`
  (`meow/include/meow/repository/security_policy.hpp`) set from config in
  `main.cpp`; also togglable via `MEOW_REQUIRE_SIGNATURE=1` for CI/tests.
- **(MEDIUM — FIXED)** ~~Empty `keyId` skips verification.~~ With
  `require_repository_signature` enabled, a present-but-empty `keyId` is now a
  hard error instead of a silent skip. (When the flag is off, the legacy
  warn-and-continue is retained for backwards compatibility.)
- **(LOW) `keyId` is a selector, not a binding.** `verifyFile` ignores
  `sig.keyId` after key selection; there is no cross-check that the claimed
  keyId matches the actual verifying key beyond "it must be a trusted key".
  Acceptable, but the field is effectively decorative in the crypto path.

### 1.2 Repository identity

- `validateRepoId` (`backend_detail.hpp:101`) enforces a non-empty id and an
  `[A-Za-z0-9._-]` charset. No check against an expected/known id set.
- Cross-repo cache is keyed by `repository_id`
  (`cacheDirFor`, `backend_detail.hpp:37`; `config.hpp:17`). Two distinct
  configured sources that reuse the same `repository_id` silently share one
  cache directory.
  **(MEDIUM)** An operator who points at two unrelated repos that happen to
  share an `repository_id` gets cache collisions / cross-contamination; and a
  malicious repo cannot be distinguished from a trusted one by id alone.
- **(LOW)** `repository_id` is not bound to the signature or to the configured
  source, so it provides provenance only by convention.

### 1.3 Expiry handling

- `checkRepoExpiry` (`backend_detail.hpp:117`) throws `RepositoryExpired` when
  `now >= expires`. Per-repo loads each call it (`backend.cpp:136`,
  `http_backend.cpp:106`). Correct for single repos.
- **(LOW)** In the merged multi-repo view, `merged.expires` is taken from the
  **first** loaded repo only (`manager.cpp:99`). Each repo is still expiry-
  checked on its own load, so this is cosmetic, but the merged metadata can
  report a stale/missing expiry.

### 1.4 Mirror failover

- `isFailoverAllowed` (`failover.cpp:7`) allows retry only for transport
  errors (timeout, interrupted, failed, 5xx). Signature/expiry/metadata/
  checksum/4xx failures **stop** the chain. This is the correct trust stance
  and is the strongest part of the design.
- Residual risk: failover trusts the *classification* in `status.cpp`. If a
  future code path throws a generic error for a trust failure, it would be
  misclassified as `NetworkError` and fall over. Keep trust errors mapped
  explicitly (they are today).

### 1.5 Cache isolation

- Per-repo caches are namespaced by `repository_id` under
  `~/.cache/meow/repos/<id>` (`backend_detail.hpp:30`). Good: mirrors of one
  source share a cache; different ids are isolated.
- **(LOW)** Cache files are written world-readable by default
  (`atomicWrite`, `backend_detail.hpp:41`); no `chmod` to restrict. On a
  multi-user host the cached `repository.toml`/`.sig` are readable by others
  (informational only, not secret).

---

## 2. Hook execution security

Reviewed: `meow/src/hooks/runner.cpp`, `meow/src/install/installer.cpp:25`
(`defaultPolicy`), `meow/src/main.cpp:507`.

### Current guarantees

- Hooks run in a **child process** (`fork` + `execve`, `runner.cpp:111`),
  isolated by a pipe for output capture and a `poll`-based timeout
  (`runner.cpp:158`).
- **Minimal environment** when `inheritEnvironment=false` (default):
  `clearenv()` then only `HOME`, `PATH=/usr/bin:/bin`, `TMPDIR`,
  `MEOW_PACKAGE/VERSION/HOOK_TYPE/HOOK_STAGING` are set
  (`runner.cpp:56`, `:143`). Builder/CI secrets (`CI`, `GITHUB_*`, `NIX_*`,
  `SSH_*`) do **not** leak — verified by the integration test
  (`07_hooks.sh`).
- **Working directory** is a per-package staging dir under
  `temp_directory_path()/meow/hooks/...` (`runner.cpp:34`, `:125`), not the
  install root.
- **Timeout** enforced: `MEOW_HOOK_TIMEOUT` (sec) overrides
  `config.hookTimeout` (default 30s, `config.hpp:65`). On timeout the child is
  `SIGTERM`'d, then `SIGKILL`'d after 2s grace (`runner.cpp:181`). Tested by
  `07_hooks.sh` (HookTimeout).
- Exit code 0 required; any non-zero aborts the install (`runner.cpp:214`).

### Limitations

- **(MEDIUM) Network isolation is NOT implemented.** `enforceNetworkPolicy`
  (`runner.cpp:68`) only logs a warning when `allowNetwork=false` (the
  default, `config.hpp:66`). Hooks therefore run with **full network access**
  today. This is explicitly documented as not-yet-implemented and is
  advisory-only. A malicious or compromised package's hook can exfiltrate data
  or fetch further payloads.
- **(LOW) Filesystem access is unbounded.** Hooks run as the invoking user
  with no chroot/namespace, so they can read/write anywhere the user can
  (e.g. `~`, other packages). The minimal `PATH` limits which binaries are
  found by name, but absolute paths are unrestricted.
- **(LOW) No resource limits** beyond wall-clock time (no CPU/mem/fs-size
  caps, no `setrlimit`).
- **(LOW)** `stagingDir` and `hook-home` are created under the shared
  `temp_directory_path()`; concurrent installs of the same package could share
  a staging path (name-collision), though `create_directories` + per-package
  naming mostly avoids it.

---

## 3. Package installation security

### 3.1 Archive extraction / path traversal

Reviewed: `meow/src/archive/archive.cpp`, used via
`installer.cpp:71,91` and `upgrade.cpp:74`.

- **(GOOD) Install path is scoped.** `extractPackageContent`
  (`archive.cpp:191`) and `extractPackageFile` (`:222`) extract **only**
  entries under the `files/` prefix; everything else is skipped. A `../`
  entry outside `files/` is never written. This blocks the common
  "../../etc/passwd" traversal for the install code path.
- **(HIGH/MEDIUM — FIXED)** ~~No libarchive secure flags.~~ Extraction now
  applies `ARCHIVE_EXTRACT_SECURE_NODOTDOT | ARCHIVE_EXTRACT_SECURE_SYMLINKS |
  ARCHIVE_EXTRACT_UNLINK` plus explicit pre-checks (`ensureSafeEntry`,
  `ensureSafeSymlink` in `archive.cpp`) that reject absolute paths, `..`
  segments, destination escapes, and symlinks whose target escapes the tree.
  (`SECURE_NOABSOLUTEPATHS` is deliberately not used because the entry pathname
  is rewritten to an absolute `destination/rel`; absolute *source* entries are
  caught by `ensureSafeEntry`.) Regression tests:
  `test/unit/archive_security_test.cpp`. Original description of the risk:
  - Symlinks/hardlinks inside the archive are extracted as links; if a
    `files/` entry is a symlink pointing outside the tree, or a later file is
    written through that symlink, a traversal/overwrite is possible
    (TOCTOU/symlink attack). The `files/`-prefix filter reduces but does not
    eliminate this for symlinked targets.
  - `ARCHIVE_EXTRACT_TIME` replays archive mtime; `ARCHIVE_EXTRACT_PERM`
    replays archive mode bits (including setuid/setgid if present).
- **(MEDIUM — FIXED)** ~~Dead-but-dangerous API.~~ The unused `extractAll` and
  `extractFile` (no `files/` scoping, raw traversal) have been **removed** from
  both `archive.hpp` and `archive.cpp`. The only extraction entry points now are
  the `files/`-scoped, secure-flag `extractPackageContent` /
  `extractPackageFile`.

### 3.2 Ownership / permissions

- Files are extracted with the **effective UID/GID** of the `meow` process
  (libarchive default; no `fchown`/ownership normalization). For a
  user-level package manager this is expected (root installs as root). No
  privilege drop or ownership policy is applied. Acceptable for the current
  single-user model; note it for any future multi-user/setuid use.

### 3.3 Atomic transactions / rollback

Reviewed: `meow/src/transaction/transaction.cpp`,
`meow/src/install/installer.cpp:76`, `meow/src/upgrade/upgrade.cpp:61`,
`meow/src/remove/remove.cpp:25`.

- **(GOOD) DB commit is atomic-ish.** Files are extracted to disk, *then*
  `commitTransaction` (`transaction.cpp:39`) writes all DB records + history
  in one pass. On exception, `rollbackTransaction` runs **before** the DB is
  committed, so the database is never left with half-registered packages.
- **(MEDIUM) Rollback is file-delete only.** `rollbackTransaction`
  (`transaction.cpp:52`) removes recorded `createdFiles` in reverse order,
  but:
  - **Directories** created during extraction are **not** removed.
  - **Hook side effects are not rolled back.** `pre_install`/`post_install`
    run *inside* the per-package loop (`installer.cpp:88,94`), i.e. during
    extraction and **outside** the DB commit. A failure after a
    `post_install` hook has run leaves that hook's filesystem/state changes
    in place. Hook execution is therefore not part of the transaction.
  - If extraction of package N partially fails mid-archive, only package N's
    listed files are rolled back (acceptable), but packages 1..N-1 already
    had their `post_install` hooks execute.
- **(MEDIUM) No install concurrency lock.** `meow install` does **not** take
  a global lock while mutating the filesystem and the DB. The `meow.lock`
  file (`main.cpp:30`) is the **reproducible-build lockfile** (`--locked`),
  not an install mutex. Two concurrent `meow install` invocations race on
  (a) non-atomic file extraction and (b) DB writes → interleaved/corrupted
  state. Recommended: an `flock`-based advisory lock around the transaction.
- **(LOW) Upgrade is extract-then-commit.** `upgrade.cpp:67` deletes old
  files *before* extracting the new archive; if extraction fails, rollback
  deletes the new (partial) files but the **old files are already gone** →
  the package ends up uninstalled rather than reverted to the old version.
  Acceptable but worth noting as a non-atomic upgrade window.

---

## 4. Download / checksum handling

Reviewed: `meow/src/download/downloader.cpp`, `meow/src/repository/resolver.cpp`.

### Current guarantees

- Artifacts are staged as `<dest>.part` and only renamed to the final path
  after a successful transfer (`downloader.cpp:13`, `finalizeDownload`).
  Partial/`.part` leftovers are removed on abort (`abortDownload`). The
  integration suite checks for stray `.part` files.
- **Checksum is enforced before use.** `resolver.cpp:29` verifies
  `sha256` after download (and re-verifies a cached copy at `:21`). A mismatch
  throws `ChecksumMismatch` and deletes the file. Good — *provided the
  expected hash is trustworthy* (see 1.1: the hash comes from the unsigned
  version manifest).
- Size caps: `maxBytes` enforces a Content-Length / actual-size limit
  (`downloader.cpp:43,185,261`). Good DoS guard.
- TLS verification **on by default** (`verifyTls=true`,
  `downloader.hpp:14`); only disabled if a caller explicitly passes
  `verifyTls=false`. No in-tree caller does. Good.
- Retry policy is transport-only (5xx / connect / timeout), 4xx is terminal
  (`downloader.cpp:90,232`). Correct.

### Limitations

- **(MEDIUM) `sha256sum` via `popen` with path interpolation.**
  `verifyChecksum` (`downloader.cpp:290`) and `computeFileHash`
  (`downloader.cpp:309`) build `sha256sum "<path>"` and pass it to `popen`.
  The path is interpolated into a shell command. A crafted archive filename
  (which originates from the manifest and thus, per 1.1, is attacker-
  influenceable) containing a double-quote or shell metacharacter can break
  parsing or inject commands. Replace with an in-process hash
  (`EVP_Digest` over the file, as `signature.cpp` already uses OpenSSL).
- **(LOW) `file://` downloads copy without checksum.** `performFile`
  (`downloader.cpp:250`) copies a local file and returns it as the result; the
  sha256 is only checked later by the caller (`resolver.cpp`). If a `file://`
  artifact's real hash differs from the manifest, the mismatch is caught
  downstream — fine, but `performFile` itself applies no integrity check.

---

## 5. Severity ranking & recommended fixes

| # | Area | Issue | Severity | Recommended fix |
|---|------|-------|----------|-----------------|
| 1 | Trust | Only `repository.toml` signed; package manifests + artifact hashes unsigned | **High** | *Deferred (format change); boundary documented in §7.* Sign a full manifest (per-package hashes); verify every loaded manifest against the signature. |
| 2 | Trust | Unsigned repos accepted; no "require signature" flag | **High** | **DONE** — `[security] require_repository_signature`; reject repos with no `.sig` when enforced. |
| 3 | Extract | No libarchive SECURE flags (symlink/hardlink/dotdot) | **High/Med** | **DONE** — `ARCHIVE_EXTRACT_SECURE_NODOTDOT \| SECURE_SYMLINKS \| UNLINK` + explicit entry/symlink checks. |
| 4 | Extract | `extractAll`/`extractFile` allow traversal (dead code) | **Med** | **DONE** — removed. |
| 5 | Download | `sha256sum` via `popen` (shell injection) | **Med** | Compute SHA-256 in-process with OpenSSL `EVP_Digest`. *(not in this pass)* |
| 6 | Hook | Network isolation unimplemented (advisory only) | **Med** | Implement via namespaces/seccomp; deny by default. *(out of scope)* |
| 7 | Transact | Rollback = file-delete only; no dir cleanup; hooks not rolled back | **Med** | Track created dirs; run hooks inside the transaction boundary. *(out of scope)* |
| 8 | Transact | No install concurrency lock | **Med** | `flock` the DB/root around the transaction. *(out of scope)* |
| 9 | Trust | Empty `keyId` skips verification | **Med** | **DONE** — hard-fail under require-signature mode. |
| 10 | Identity | `repository_id` only charset-checked; cache keyed by id | **Med/Low** | Bind id to configured source; detect id reuse/collision. |
| 11 | Upgrade | Old files removed before new extracted (non-atomic window) | **Low** | Extract to temp, swap atomically, keep old until commit. |
| 12 | Trust | `merged.expires` from first repo only | **Low** | Merge/propagate per-repo expiry in `buildMerged`. |
| 13 | Cache | Cache files world-readable | **Low** | `chmod 600/700` the cache dir/files. |

### Items intentionally deferred (documented limitations)

- Hook network/filesystem sandboxing: explicitly out of scope for v0.5
  (operator must run in a sandboxed environment). Relevant code paths are
  flagged with warnings so failure is loud, not silent.
- Multi-user / setuid ownership model: not supported; ownership follows the
  invoking user by design.

---

## 7. Remediation status (v0.5 hardening pass)

This pass is **security-only** (no features, no resolver/SAT changes). Fixes:

| # | Finding | Severity | Status | Where |
|---|---------|----------|--------|-------|
| 3 | No libarchive SECURE flags (symlink/dotdot/abs traversal) | High/Med | **Fixed** | `archive.cpp`: `secureExtractFlags`, `ensureSafeEntry`, `ensureSafeSymlink` |
| 4 | `extractAll`/`extractFile` allow traversal (dead code) | Med | **Fixed (removed)** | `archive.{hpp,cpp}` |
| 2 | Unsigned repos accepted; no require-signature flag | High | **Fixed** | `[security] require_repository_signature`; `security_policy.{hpp,cpp}`; `verifyRepoSig` |
| 9 | Empty `keyId` skips verification | Med | **Fixed (under require mode)** | `verifyRepoSig` |
| 1 | Package manifests + artifact hashes unsigned | High | **Documented boundary** (needs format change) | §1.1, below |

### Item 1 — package-metadata trust boundary (deferred, documented)

Only `repository.toml` is signed. Per-package `package.toml` and
`versions/<ver>.toml` (which carry the artifact `sha256`) are **not** individually
signed. Closing this fully requires a signed manifest of all package hashes — a
repository **format change** that is out of scope for this hardening pass (the
task explicitly scoped out format redesign).

**Current trust boundary (until signed manifests land):**

- The download-time `sha256` check is only as trustworthy as the *transport*
  that delivered the version manifest, **not** as trustworthy as the signature.
- For `http(s)://` repositories, operators MUST rely on TLS (on by default) to
  protect manifest integrity in transit; a malicious/compromised server or a
  successful MITM can substitute both metadata and a matching hash.
- For `file://` repositories, the manifests are as trustworthy as the local
  checkout.
- `require_repository_signature` guarantees the **index** (`repository.toml`) is
  authentic; it does **not** yet authenticate per-package hashes.

Mitigation shipped this pass: require-signature mode ensures the signed index
cannot be silently replaced, and the secure extractor limits blast radius if a
malicious artifact is nonetheless installed. Full remediation (signed
per-package hashes) is tracked for a dedicated format-versioned change.
Detection of index tampering is covered by `02_repo_metadata.sh`.

---

## 6. Missing regression tests (test gaps)

The integration suite (`test/integration/sections/`) already covers signature
rejection, expiry, missing-key, and tamper detection for `repository.toml`
(`02_repo_metadata.sh`). It does **not** cover:

- Extraction of a malicious archive containing `../`, symlinks, or
  setuid bits → assert rejection / safe extraction.
- A repo that ships package metadata whose `sha256` does **not** match the
  signed index (would require signed manifests first) → end-to-end MITM test.
- `verifyTls=false` path and a self-signed/modified cert → assert MITM is
  detected when TLS is on.
- Concurrent `meow install` → assert no corruption / lock contention.
- Hook network egress → assert it is blocked once sandboxing lands.
- `sha256sum` path with a hostile filename → assert no shell injection.

These should be added alongside the corresponding fixes above (separate
change; not part of this audit).
