# Security Testing

How to run and extend the meowOS security regression suite.

## Overview

The suite is a permanent guard against regressions in the security-critical
paths reviewed in [`docs/security-audit-v0.5.md`](security-audit-v0.5.md):

- archive extraction (path traversal, absolute paths, symlink attacks)
- repository trust (unsigned-repo rejection, tampered / invalid metadata)
- installation (concurrent-install robustness, rollback cleanup)
- hooks (network access attempt reporting)

It runs **only against the built binaries** (`meow`, `meow-build`, `meow-repo`)
and the existing C++ security unit tests. It never modifies production code.

## Running

From the repository root, after building:

```bash
# build first (CMake)
cmake -B build -S . && cmake --build build -j"$(nproc)"

# run the security suite
./test/security/run.sh

# or point at a custom build dir
MEOW_BUILD=/path/to/build ./test/security/run.sh
```

A non-zero exit code means at least one security check failed.

The C++ unit tests that back the archive and policy checks are also runnable
directly (and via CTest):

```bash
./build/meow-unit-archive-security     # archive traversal / absolute / symlink
./build/meow-unit-security-policy      # unsigned-repo rejection in strict mode
```

## What is covered

| # | Area | Test | Asserts |
|---|------|------|---------|
| 1 | Archive | `meow-unit-archive-security` | `../` traversal, absolute-path entry, escaping symlink, and absolute symlink targets are all rejected; a valid package still extracts |
| 2 | Trust | `meow-unit-security-policy` | unsigned repo loads by default; rejected with `InvalidSignature` when `requireRepositorySignature` is set |
| 3 | Trust | `test/security/run.sh` — strict mode | binary-level: unsigned repo rejected (`InvalidSignature`) under `security.require_repository_signature = true`; loads under default policy |
| 4 | Trust | `test/security/run.sh` — tampered metadata | editing a signed `repository.toml` after signing is detected (`InvalidSignature`) |
| 5 | Trust | `test/security/run.sh` — invalid hash | a version manifest declaring the wrong `sha256` is rejected at download time (`ChecksumMismatch`) |
| 6 | Install | `test/security/run.sh` — rollback | a failed install (hook error) leaves the package unregistered and removes extracted files |
| 7 | Install | `test/security/run.sh` — concurrent | two concurrent installs of the same package terminate and leave the DB queryable (no corruption / wedged lock) |
| 8 | Hooks | `test/security/run.sh` — network reporting | a hook that attempts network egress triggers meow's "network isolation unavailable" advisory warning; the install still completes (isolation is advisory only — see audit item on hook sandboxing) |

## How the suite is isolated

`test/security/run.sh` sets up, per run:

- a fresh `HOME` with only the test trusted key installed (never touches the
  real user keystore);
- a temp artifact directory and a temp install root (`/tmp/meow-install`,
  which the binary hardcodes as its install destination);
- a unique database and config per test, all under `mktemp` dirs.

Each test cleans up the install root so extracted files never leak between
cases. No test writes outside its scratch space.

## Extending the suite

Add a new `t_*` function in `test/security/run.sh` and call it from `main`.
Helpers available:

- `make_signed_repo <name> <version> <repoDir> [hookScript]` — builds a signed
  repo with one package (optionally embedding a `post_install` hook).
- `write_config <repoUrl> <extraTomlLines> <outPath>` — emits a `[[repositories]]`
  config; pass multi-line extras (e.g. a `[security]` block) as real newlines.
- `run_meow <meow args...>` — invokes `meow` under the isolated `HOME`, merging
  stdout/stderr for assertions.
- `ok "message"` / `bad "message"` — record PASS/FAIL.

When adding a new archive or trust check that needs the C++ API directly
(symlink hardening, signed per-package manifests, etc.), extend
`test/unit/archive_security_test.cpp` or `test/unit/security_policy_test.cpp`
and register the new binary in `CMakeLists.txt` + CTest as the existing ones
are.

## Known limitations (tracked in the audit)

These are intentionally exercised as *behavior* checks, not yet enforced as
hard failures, because the underlying hardening is not implemented:

- Hook network isolation is **advisory** — the suite asserts the warning is
  emitted, not that egress is blocked. Once sandboxing lands, strengthen this
  to assert no egress occurs.
- Concurrent install is asserted to be *non-corrupting / non-hanging*, not to
  be serializable. Once an install lock is added, tighten the assertion to
  require exactly-once registration.
- Per-package manifests and artifact hashes are covered only at the signed
  `repository.toml` level (audit item 1). When signed manifests land, add a
  test that tampering a `by-name/.../versions/*.toml` hash is detected.
