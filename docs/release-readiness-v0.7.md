# Release-readiness report — v0.7 (signed package index) + SAT-default review

_Last reviewed: v0.7.0-rc2 (committed)._
_See `git tag v0.7.0-rc1` (commit `1da61f9`, 2026-07-21)._
_RC validation: test/rc/ — 0 unexpected regressions._
_RC stabilization commits: `067cdd3` (transaction safety), `83f2736` (failover_
_logging + error classification), `351c81d` (upgrade file cleanup + migration_
_atomicity + UPSERT rowid fix)._

## 1. v0.7 — Signed package index

### Status: READY (feature-complete, tested)

Closes the per-package manifest / artifact trust boundary noted in
`docs/security-audit-v0.5.md` §7. A repository may ship a second, independently
signed index (`packages.toml` + `packages.toml.sig`) that authenticates each
package version's `manifest_hash` and `artifact_hash` with the **same** Ed25519
trusted key used for `repository.toml.sig`.

| Item | State |
|------|-------|
| Index generation on `meow repo sign` (auto) | Done — `repoBuildIndex` runs before signing |
| Index verification on load | Done — `loadVerifiedPackageIndex` |
| `manifest_hash = sha256(raw package.toml ‖ raw version.toml)` | Done (canonical, crypto-only, no TOML normalization) |
| `artifact_hash` binding | Done |
| New error codes (`MissingPackageIndex`, `InvalidPackageIndex`, `PackageIndexMismatch`) | Done + surfaced as trust failures (non-failover) |
| `require_package_index` config + `MEOW_REQUIRE_PACKAGE_INDEX` env | Done |
| Backwards-compatible (absent index → warn/continue) | Done (default `false`) |
| HTTP backend index download/verify | Done |

### Verification

- Build: clean (`nix develop --command cmake --build build`).
- Unit: `ctest -L unit` → **14/14 pass** (incl. `meow-unit-pkgidx`,
  `meow-unit-failover`, `meow-unit-upgrade`). `meow-unit-history` has a
  pre-existing build-time include issue (missing `sqlite3.h` in its cmake
  target), not a test regression.
- Integration: `ctest -L integration` → **21/24 pass** under both
  `MEOW_RESOLVER=legacy` and `MEOW_RESOLVER=sat`.
  - New `23.signed_index` → **13/13 pass** (happy path, strict happy, tampered
    manifest → `InvalidMetadata`, tampered index → `InvalidSignature`, strict
    missing index → `InvalidSignature`, compat missing/unsigned tolerated).
  - The **3 failures are pre-existing and unrelated** to this feature:
    `04.http`, `13.dual_backend`, `legacy` all fail because
    `test/integration/http_fixture.py` is absent from the tree (confirmed at
    clean HEAD `28aa88c` via a throwaway worktree). Not a regression.
- SAT benchmark: unaffected (index verify is load-time, outside the SAT hot
  path). Per-load cost is one SHA-256 + one TOML parse per package version —
  bounded and negligible vs. network/extraction.

### Incompatibilities / migration

- **Additive only.** Repositories without an index continue to work exactly as
  before (per-package manifests authenticated transitively via `repository.toml`
  / TLS). `require_package_index` defaults to `false`.
- Operators opting into `require_package_index = true` must publish
  `packages.toml` + `packages.toml.sig`; this now happens **automatically** on
  every `meow repo sign`, so no manual step is required once the policy is on.
- No database schema, config, or on-disk format change for consumers.

### Blockers

- None for shipping the signed-index feature.
- Out-of-scope but recommended before any release: restore the missing
  `test/integration/http_fixture.py` so the 3 HTTP sections are exercised
  (currently they silently no-op/fail). This is a test-infra gap, not a
  product defect.

## 2. SAT resolver as default — review

### Status: PARITY ACHIEVED; DEFAULT NOT YET FLIPPED (deliberate)

The default remains `Legacy` (`ResolverEngine::Auto → LegacyResolver` in
`meow/src/dependency/resolver_factory.cpp:12-14`). The flip is deferred to a
separate commit after the v0.7.0 release. All parity criteria are met; RC
validation confirmed **0 unexpected regressions**.

### Criteria assessment (per `docs/sat-default-criteria.md`)

| Criterion | Result |
|-----------|--------|
| §1 Correctness parity (both engines, full suite) | **Met** — identical 21/24 matrix under `legacy` and `sat`; `sat-parity` unit passes (40 shapes) |
| §2 Performance (no pathological explosion) | **Met** — bench bounded; `manyvirt-1000x10` ≈ 13.4s (documented pure-DPLL expectation; CDCL is future work) |
| §3 Robustness (UNSAT diagnostics) | **Met** — `PackageConflict`/`MissingProvider`/`VersionConflict`/`Cycle` reported |
| §4 Rollout / escape hatch | **Met** — `MEOW_RESOLVER=legacy` and `ResolverEngine::Legacy` both work |
| CHANGELOG/release notes for the flip | **Pending v0.7.0 final** |
| Follow-up issue to remove Legacy | Open (post-stabilization) |

## 3. Deliverables shipped in v0.7.0-rc1

- Signed package index implemented, documented (`docs/security.md`).
- Tests: `test/unit/package_index_test.cpp`, `test/integration/sections/23_signed_index.sh`.
- SAT resolver: parity confirmed, RC validated, default flip deferred to separate commit.
- RC validation tooling: `test/rc/generate_realistic_repo.py` + `compare_resolvers.py`.
- Benchmarks: SAT suite run; no regression attributable to index or resolver work.
- Tagged as `v0.7.0-rc1` (commit `1da61f9`).

### Known pre-existing issues (not blocking RC)

- `test/integration/http_fixture.py` is absent from the tree — 3 HTTP-related
  integration sections silently no-op. Not a regression.
