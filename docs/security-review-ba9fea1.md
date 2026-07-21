# Security Review — commit ba9fea1

Scope: review of `ba9fea1` ("security: harden package extraction and repository trust checks").
No code was modified during this review. Findings below are against the diff as
committed. Severity: **High / Medium / Low**. Each finding lists a concrete
recommendation.

The commit adds:
- `SecurityPolicy::requireRepositorySignature` (config `[security] require_repository_signature`, env `MEOW_REQUIRE_SIGNATURE`) enforced in `verifyRepoSig`.
- libarchive `SECURE_NODOTDOT` / `SECURE_SYMLINKS` / `UNLINK` flags plus explicit `ensureSafeEntry` / `ensureSafeSymlink` pre-checks in `archive.cpp`.
- Removal of the unscoped `extractAll` / `extractFile` helpers.
- Two new C++ unit tests (`archive_security_test.cpp`, `security_policy_test.cpp`).

---

## 1. Does `requireRepositorySignature` enforce all intended paths?

**Mostly yes, with one classification gap.**

- Enforcement is centralized in `verifyRepoSig` (`backend_detail.hpp:149,161`),
  which is the single signature-verification entry point for **both** the
  filesystem backend and the HTTP backend (`http_backend.cpp:103`). So strict
  mode applies uniformly to local and remote repos. Good.
- The three intended paths are covered by code:
  - no `.sig` present → `InvalidSignature` when strict (`backend_detail.hpp:150`).
  - empty `keyId` → `InvalidSignature` when strict (`backend_detail.hpp:162`).
  - invalid signature → already rejected unconditionally (pre-existing).

**Findings**

- **(MEDIUM) Corrupt `.sig` is misclassified, bypassing strict-mode intent.**
  `verifyRepoSig` calls `crypto::loadSignature(sigPath)` with no `try/catch`.
  `loadSignature` (`signature.cpp:22`) calls `toml::parse_file`, which throws a
  `toml++` exception on malformed TOML. That exception escapes `verifyRepoSig`,
  so the repo fails to load with an **unclassified** error (`classifyRepositoryError`
  default → `Unavailable`), *not* `InvalidSignature`. Under `requireRepositorySignature`
  the operator expects a signature failure; instead they get a generic load
  error. The repo still fails closed (not loaded), but the trust failure is
  invisible and the strict-mode branch is never exercised.
  - *Recommend:* wrap `loadSignature` in `verifyRepoSig` and, when strict mode
    is on, convert any signature-parse failure to `InvalidSignature` explicitly.

- **(LOW) Empty-`keyId` default behavior is still permissive.** Outside strict
  mode, an empty `keyId` only logs a warning and skips verification
  (`backend_detail.hpp:168`). This is the documented legacy gap (audit #9) and
  strict mode is the intended mitigation, so it is acceptable — but it should be
  called out that *only* opting in to `require_repository_signature` closes it.

- **(LOW) Policy is process-global and set once in `main`.** `setSecurityPolicy`
  writes a global; `ctest -j` runs each test in its own process so this is safe,
  but any in-process reuse must remember to reset it (the unit test does reset).
  No change needed; noted for future callers.

---

## 2. Are there remaining unsigned metadata paths?

**Yes — the largest gap from the audit is unaddressed.**

- Only `repository.toml` is signed (`meow-repo sign` signs that one file).
  Per-package manifests (`by-name/<name>/package.toml`) and **version artifacts**
  (`by-name/<name>/versions/<ver>.toml`, which carry the artifact `sha256`) are
  **not signed**. `ba9fea1` does not change this.
- Consequence: a network-position attacker (or a malicious mirror that the
  failover logic cannot distinguish, since failover only sees transport vs trust
  errors) can rewrite a version manifest's `sha256` to match an attacker-controlled
  artifact. The download-time check (`resolver.cpp` → `verifyChecksum`) then
  validates against the *attacker-chosen* hash and passes. The signed index does
  not cover package hashes, so this is not detected. This is audit item **#1**
  (High) and remains open.
  - *Recommend:* sign a complete manifest (per-package hashes) or a signed tree
    index, and verify every loaded `by-name/...` manifest/version artifact
    against that signature. Add a regression test that tampering a
    `versions/*.toml` hash is detected (currently only `repository.toml` tamper
    is covered).

- **(LOW) `repository_id` is still only charset-validated** (audit #10). Strict
  mode does not bind `id` to the configured source or detect id reuse/collision
  in the cache namespace. Out of scope for this commit but worth tracking.

---

## 3. Are archive security checks complete?

**Good coverage of the classic cases; several entry types are untested/unhandled.**

Code changes are sound: `ensureSafeEntry` rejects absolute paths and `..`
components; `ensureSafeSymlink` rejects absolute and escaping symlink targets;
`SECURE_NODOTDOT` / `SECURE_SYMLINKS` / `UNLINK` are set. Removed `extractAll` /
`extractFile` eliminate the previously-unscoped extraction paths.

**Findings**

- **(MEDIUM) Hardlink attacks are not explicitly handled or tested.** The secure
  flags (`SECURE_SYMLINKS`) cover symlink write-through, and modern libarchive
  extends that to hardlinks, but there is **no test** for a hardlink entry
  pointing outside the tree. Recommend adding a hardlink regression test (and
  confirm libarchive refuses it; if not, add an explicit hardlink check like the
  symlink one).

- **(MEDIUM) Device nodes / FIFOs are not rejected.** `archive_read_extract` with
  these flags still creates character/block devices and FIFOs if the archive
  contains them. A malicious archive could drop a device node. Recommend adding
  `AE_IFREG`/`AE_IFLNK`/`AE_IFDIR`-only filtering (reject `AE_IFCHR`, `AE_IFBLK`,
  `AE_IFIFO`) and a test.

- **(MEDIUM) Setuid/setgid bits are preserved via `ARCHIVE_EXTRACT_PERM`.** A
  package can ship a setuid binary; extraction replays the mode. For a user-level
  PM this may be intended, but it should be a **conscious decision**: either
  strip setuid/setgid on extraction or explicitly allowlist it. At minimum, add a
  test asserting the chosen behavior so it cannot regress silently.

- **(LOW) `ARCHIVE_EXTRACT_TIME` replays archive mtime.** Minor; could matter for
  cache/stat-based logic. Optional to drop.

- **(LOW) No positive test for a legitimate in-tree symlink.** All symlink tests
  are malicious. Add one asserting a symlink whose target stays *inside* the
  destination still extracts, so the secure flags are confirmed non-overzealous.

- **(LOW) `..` in the middle of a path** (e.g. `files/usr/../bin/x`) is caught by
  the `..` loop in `ensureSafeEntry`, but there is no dedicated test. Add one for
  completeness.

---

## 4. Are tests sufficient?

**Adequate as a first regression layer; several committed-code paths are untested.**

`archive_security_test.cpp` (5 cases) and `security_policy_test.cpp` (2 cases)
pass and cover the headline scenarios. Gaps relative to what the commit
implements:

- **(MEDIUM) Empty-`keyId` strict rejection is untested.** `backend_detail.hpp:162`
  adds this branch but no test exercises a signature with an empty `keyId` under
  `requireRepositorySignature`. Add a unit test (build a signed-looking `.sig`
  with `keyId = ""`).

- **(MEDIUM) Tampered signed `repository.toml` is untested at unit level.** The
  audit's "modified package metadata" scenario (sign, then edit, then expect
  `InvalidSignature`) is only covered by the shell-level suite
  (`test/security/run.sh`), not by a C++ unit test bundled with this commit.
  A unit test keeps the regression next to the code.

- **(MEDIUM) `MEOW_REQUIRE_SIGNATURE` env path is untested.** `main.cpp:384`
  reads the env override; no test asserts it flips the policy. Add a small
  integration/unit check.

- **(MEDIUM) HTTP-repo strict rejection is untested.** `http_backend.cpp:103`
  shares `verifyRepoSig`, so strict mode should reject an unsigned HTTP repo,
  but no test covers it. Covered only by `test/security/run.sh` end-to-end.

- **(LOW) Corrupt-`.sig` path (finding 1) has no test** and currently fails
  closed with the wrong error class — add a test once the classification is fixed
  so it asserts `InvalidSignature`.

- **(LOW) Archive gaps from §3** (hardlink, device, setuid, in-tree symlink) have
  no tests.

**Positive:** the shell suite `test/security/run.sh` (separate commit) already
exercises strict-mode rejection, tampered metadata, invalid hash, rollback,
concurrent install, and hook network reporting end-to-end, partially compensating
for the missing unit tests above. Recommend folding the most important of these
(unsigned + tampered + empty-keyId + HTTP) into C++ unit tests so the regression
lives with the code it guards.

---

## Summary of recommendations (priority order)

1. **(High, tracked separately)** Sign per-package manifests / artifact hashes
   (audit #1); `ba9fea1` does not address it.
2. **(Medium)** Make a corrupt `.sig` classify as `InvalidSignature` under strict
   mode (wrap `loadSignature`).
3. **(Medium)** Add archive tests + handling for hardlinks, device/FIFO nodes,
   and setuid bits.
4. **(Medium)** Add unit tests for: empty-`keyId` strict rejection, tampered
   `repository.toml`, `MEOW_REQUIRE_SIGNATURE` env, and HTTP-repo strict rejection.
5. **(Low)** Add positive in-tree-symlink test; `..`-mid-path test; reset/isolate
   global policy in tests; document that only opting in to
   `require_repository_signature` closes the empty-keyId gap.
