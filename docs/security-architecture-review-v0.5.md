# meowOS Security Architecture Review — v0.5

Scope: post-commit review of `ba9fea1` (harden extraction + trust checks) and
`9f3e6ca` (close remaining extraction + signature gaps). No code changes were
made for this review; it is a static analysis of the current tree plus the
design in `docs/package-signing-design.md`.

Code reviewed: `meow/src/archive/archive.cpp`, `meow/src/crypto/signature.cpp`,
`meow/src/repository/backend_detail.hpp`, `meow/src/repository/failover.cpp`,
`meow/src/repository/http_backend.cpp`, `meow/src/transaction/transaction.cpp`,
`meow/src/crypto/keystore.cpp`, `meow/src/main.cpp`, and the design doc.

---

## 1. Current security posture

The v0.5 hardening pass materially improved the trust and extraction posture:

- **Extraction is now fail-closed.** `archive.cpp` applies
  `SECURE_NODOTDOT | SECURE_SYMLINKS | UNLINK` plus explicit pre-checks
  (`ensureSafeEntry`, `ensureSafeLink`, `ensureSafeFiletype`,
  `ensureSafePerm`) that reject absolute paths, `..` traversal, symlink/hardlink
  escapes, device nodes, FIFOs, and setuid/setgid bits. `extractAll` /
  `extractFile` (the unscoped entry points) were removed.
- **Signatures fail closed on malformed input.** `loadSignature` now parses the
  `.sig` TOML inside a try/catch and throws `InvalidSignature` on parse failure
  instead of leaking a parser exception that could bypass the policy.
- **Unsigned / empty-keyId repos are enforceable.** `require_repository_signature`
  (config + `MEOW_REQUIRE_SIGNATURE`) makes a missing `.sig` or empty `keyId` a
  hard `InvalidSignature` error.
- **Failover preserves trust.** `isFailoverAllowed` only retries on transport
  errors; signature/expiry/metadata/checksum/4xx failures stop the chain. A
  trust failure never falls over to another mirror.

**Standing gap (unchanged from audit):** package manifests and the artifact
`sha256` they carry are still unsigned. The download-time checksum validates
against a hash taken from the **unsigned** version manifest. This is HIGH #1,
deferred, and now has a concrete design (`docs/package-signing-design.md`).

---

## 2. Section-by-section analysis

### 2.1 Repository trust chain

- **Authenticated:** `repository.toml` (identity, mirrors, expiry) via Ed25519
  against a trusted key; `repository_id` charset + expiry after verify.
- **Still trusted from mirrors:** `by-name/<name>/package.toml`,
  `versions/<ver>.toml` (carries `artifact.sha256`), and the artifact bytes
  themselves (hash comes from the unsigned manifest). A mirror that substitutes
  these passes today's chain because the hash it forges is the one the checksum
  validates against.
- **TOCTOU risks:** low for signature (atomic `.part`+rename on cache write,
  re-verify on every load). Extraction uses `UNLINK` + `SECURE_SYMLINKS` which
  closes the classic symlink-follow TOCTOU. One residual note: extraction runs
  as the invoking user with no chroot; a *pre-existing* symlink outside the tree
  pointing into it is handled by `SECURE_SYMLINKS`/`UNLINK`, but combined with
  the hook network gap (§2.4) a malicious package could still race — acceptable
  for the single-user model, see ranked risks.
- **Failover trust guarantee:** preserved. Trust failures (`InvalidSignature`,
  `Expired`, `InvalidMetadata`, `ChecksumMismatch`) are non-failover.

### 2.2 Signature enforcement

- `requireRepositorySignature`: enforced in `verifyRepoSig`; missing `.sig` and
  empty `keyId` both hard-fail under the flag. Correct.
- Empty `keyId` handling: when flag is off, warns and skips (legacy compat);
  when on, hard error. Consistent with the audit fix.
- Malformed signature handling: `loadSignature` now throws `InvalidSignature`
  on TOML parse failure — closes the documented "corrupt .sig bypass" path.
- HTTP backend: downloads `repository.toml.sig`, tolerates absence (matches
  filesystem), then calls the same `verifyRepoSig`. Parity is good.
- Filesystem backend: identical `verifyRepoSig` path.
- **Inconsistency / bypass path to watch:** `verifyFile` returns `false` (not
  throw) for *missing key file* (`loadTrustedKey` throws `TrustedKeyNotFound`,
  which propagates) — fine. But note `verifyFile` ignores `sig.keyId` after key
  selection (audit LOW): the claimed `keyId` is never cross-checked against the
  verifying key's identity beyond "it must be a trusted key." Acceptable today.
- **Missing tests:** no regression test yet for a *malformed* `.sig` (the
  fail-closed parse path) or for `TrustedKeyNotFound` under `require` mode.
  Unit coverage exists for policy + signing, but the malformed-sig branch in
  `signature.cpp:28` is untested.

### 2.3 Archive extraction security

- Path traversal: blocked by `SECURE_NODOTDOT` + `ensureSafeEntry` (rejects
  absolute, `..`, and lexical-escape). Explicit policy, fail-closed. Good.
- Symlinks: `SECURE_SYMLINKS` refuses writing through existing symlinks;
  `ensureSafeLink` refuses creating a symlink whose target escapes the tree.
- Hardlinks: `ensureSafeLink` also covers hardlink targets (aliasing attack) —
  good, this was called out as an attack vector and is handled.
- Device/FIFO: `ensureSafeFiletype` rejects `AE_IFCHR/AE_IFBLK/AE_IFIFO`. Good.
- Permission filtering: `ensureSafePerm` rejects setuid/setgid (06000). Good.
- **Dangerous flags:** none remain. `SECURE_NOABSOLUTEPATHS` is intentionally
  omitted (paths are rewritten to absolute `destination/rel` and then
  re-checked) — defensible, documented in-code.
- **Future footgun:** `verifyFile` / extraction assumes the invoking user's
  umask and the archive's `PERM`/`TIME` flags (`ARCHIVE_EXTRACT_PERM | TIME`)
  replay mode/mtime. Replaying mtime is mostly cosmetic; replaying mode bits is
  fine because setuid/setgid are stripped. No privilege drop, so root installs
  as root — acceptable for single-user, flagged for multi-user future.

### 2.4 Install transaction security

- Flow download → verify (sha256) → extract → install: correct order; checksum
  runs before use.
- Rollback: `rollbackTransaction` deletes recorded `createdFiles` only —
  **directories are not removed**, and **hook side effects are not rolled back**
  (hooks run inside the per-package loop, outside the DB commit).
- Concurrent install: **no install mutex.** Two concurrent `meow install`
  invocations race on extraction and DB writes → interleaved/corrupted state.
- DB/FS consistency: DB commit is atomic-ish (all records written in
  `commitTransaction`), but extraction precedes commit, so a crash between
  extract and commit leaves files on disk without DB records (orphan files).
- **Classification:**
  - Extraction security: **fixed** (this pass).
  - Rollback = file-delete only (no dir cleanup, hooks outside txn):
    **acceptable risk** for single-user; **future work**.
  - No install concurrency lock: **future work** (medium).
  - Hook network/filesystem sandbox unimplemented: **acceptable risk /
    future work** (documented, advisory only).

### 2.5 Future package index signing (vs `docs/package-signing-design.md`)

The proposed signed `packages.toml` + `packages.toml.sig` design is compatible
with the current architecture:

- **`repository_id`:** unchanged; index is additive, same identity anchor.
- **Mirrors:** one signature per logical repo, served by all mirrors; divergence
  fails client verification and is non-failover. Compatible with mirror groups.
- **Cache layout:** extends the per-`repository_id` cache with
  `packages.toml`/`.sig`, re-verified on every load. Fits the existing
  atomic-write + re-verify model.
- **Offline verification:** only the trusted key + two signed files needed.
  Preserved.
- **Existing signature model:** reuses `crypto::verifyFile` / `loadSignature` /
  `loadTrustedKey` and the same Ed25519 keypair. No new crypto.

The design correctly does **not** sign every file individually and keeps the
identity index small. Recommendation (signed package index) is sound and is the
right vehicle to close HIGH #1.

---

## 3. Remaining risks (ranked)

| # | Risk | Severity | Status |
|---|------|----------|--------|
| 1 | Package manifests + artifact `sha256` unsigned; malicious mirror/MITM can forge metadata + matching hash (HIGH #1) | **High** | Deferred — design ready (`docs/package-signing-design.md`); needs format change |
| 2 | No install concurrency lock; concurrent `meow install` can corrupt FS + DB | **Medium** | Future work |
| 3 | Rollback is file-delete only: dirs not removed, hook side effects not rolled back | **Medium** | Acceptable (single-user) / future work |
| 4 | Hook network + filesystem isolation unimplemented (advisory only) | **Medium** | Documented limitation / future work |
| 5 | `sha256sum` via `popen` with path interpolation (shell injection from attacker-influenced filename) | **Medium** | Future work (in-process `EVP_Digest`) |
| 6 | `keyId` is a selector, not a binding (no cross-check to verifying key) | **Low** | Acceptable |
| 7 | `merged.expires` taken from first repo only (cosmetic) | **Low** | Future work |
| 8 | Cache files world-readable (no `chmod`) | **Low** | Future work |
| 9 | Malformed `.sig` parse-fail path and `TrustedKeyNotFound` under require mode lack regression tests | **Low** | Test gap |
| 10 | `repository_id` not bound to configured source; id reuse → cache cross-contamination | **Low/Med** | Future work (bind id to source) |

No **Critical** issues remain after the v0.5 hardening pass.

---

## 4. Recommended security roadmap

### v0.6 (current series — mirrors + transport)
- Ship the v0.5 hardening as-is.
- **Add regression tests** for the new fail-closed paths: malformed `.sig`
  (`signature.cpp:28`), `TrustedKeyNotFound` under `require` mode, and a
  tampered `package.toml` detection scaffold.
- Close **#5** (`popen` sha256 → in-process OpenSSL hash) — it is a real
  injection sink fed by attacker-influenced filenames and is low-effort.

### v0.7 (SAT + signed package index)
- Implement `docs/package-signing-design.md`: emit + verify `packages.toml`/
  `packages.toml.sig`. **This closes HIGH #1.**
- Add `require_package_index` (parallel to `require_repository_signature`),
  default off for backwards compat.
- Add integration tests: signed-index happy path; tampered manifest/artifact
  hash rejected; missing index under strict mode rejected.

### v0.8 (package signing + reproducible ecosystem)
- Promote `require_package_index = true` to a supported hardening mode.
- Add the **install concurrency lock** (#2) and **rollback improvements** (#3:
  track created dirs, run hooks inside txn).
- Bind `repository_id` to the configured source (#10) to prevent cache reuse
  collisions.

---

## 5. Ready for production?

**Conditional yes** for the single-user, trusted-mirror (or TLS-protected HTTP)
model:

- Extraction and signature enforcement are now fail-closed and well-tested
  enough for general use.
- The one **High** remaining risk (unsigned package metadata) is a known,
  documented boundary that is mitigated today by TLS for HTTP repos and by local
  trust for `file://`. It is not a regression; it is the next planned change.

**Not yet production-ready** if any of these hold:
- You must defend against a *compromised or malicious mirror* without relying on
  TLS — that requires the v0.7 signed package index (HIGH #1).
- Multiple `meow install` processes can run concurrently — needs the #2 lock.
- Packages may run hooks that must be network/filesystem isolated — needs the
  sandbox (#4).

**Security blockers requiring immediate code change:** none. All remaining
items are either deferred-by-design (HIGH #1, has a plan) or Medium/Low future
work that does not undermine the current trust chain.
