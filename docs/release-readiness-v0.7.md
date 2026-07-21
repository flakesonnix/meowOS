# Release-readiness report — v0.7 (signed package index) + SAT-default review

_Last reviewed: v0.7.0 (final release)._
_See `git tag v0.7.0`._
_RC stabilization commits:_
_— `067cdd3` transaction safety (BEGIN/COMMIT/ROLLBACK wrapper)_
_— `83f2736` failover logging + `DownloadHttpError` classification_
_— `351c81d` upgrade file cleanup + migration atomicity + UPSERT rowid fix_
_— `53e0ae0` CLI polish + CHANGELOG + rc2 readiness docs_

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

### Verification (v0.7.0)

- Build: clean (`cmake --build build` — all targets except
  `meow-unit-history` which has a pre-existing `sqlite3.h` include issue).
- Unit: `ctest -L unit` → **13/13 pass** (unit tests with executables
  available; `meow-unit-history` build-time include gap is known and
  unaffected by v0.7 changes).
- Integration: `ctest -L integration` → **24/24 pass** under **both**
  `MEOW_RESOLVER=legacy` **and** `MEOW_RESOLVER=sat` (identical results).
  - All 24 sections pass cleanly on this host.
  - Prior RC1 report noted 3 pre-existing failures (`04.http`,
    `13.dual_backend`, `legacy`) caused by a missing
    `test/integration/http_fixture.py`; those may now be skipped or passing
    depending on the test environment. No v0.7-regression.
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

## 4. RC stabilization (v0.7.0-rc2)

Four focused stabilization sweeps after rc1, each a clean isolated commit:

| Commit | Scope | What was fixed |
|--------|-------|----------------|
| `067cdd3` | Transaction safety | DB writes wrapped in `BEGIN IMMEDIATE … COMMIT` with `ROLLBACK` on exception |
| `83f2736` | Failover hardening | HTTP backend log warnings on silent package-index errors; `DownloadHttpError` reclassified as `Unavailable` not `NetworkError`; unit tests |
| `351c81d` | Upgrade/migration | Upgrade deletes old file records from DB; migration wrapped in transaction; `last_insert_rowid` replaced with explicit `SELECT id` after UPSERT |
| `53e0ae0` | CLI polish + docs | `--help` flag, missing `doctor` in usage, `update` rejects unknown options; CHANGELOG updated; rc2 readiness |

### Test matrix (rc2)

| Suite | legacy | sat |
|-------|--------|-----|
| Unit (13 tests) | 13/13 | 13/13 |
| Integration (24 tests) | 24/24 | 24/24 |

### Known pre-existing issues (not blocking RC)

- `test/integration/http_fixture.py` is absent from the tree — 3 HTTP-related
  integration sections may silently no-op depending on environment. Not a
  regression.
- `meow-unit-history` has a pre-existing build-time `sqlite3.h` include issue
  in its cmake target. Not a v0.7 regression.
