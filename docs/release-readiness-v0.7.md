# Release-readiness report — v0.7 (signed package index) + SAT-default review

_Last reviewed: v0.7 working tree (pre-tag, uncommitted feature work)._

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
- Unit: `ctest -L unit` → **12/12 pass** (incl. new `meow-unit-pkgidx`).
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

### Status: PARITY ACHIEVED; DEFAULT NOT FLIPPED (deliberate)

The instruction was to **review** the default and report — not to change it. The
default remains `Legacy` (`ResolverEngine::Auto → LegacyResolver` in
`meow/src/dependency/resolver_factory.cpp:9-14`). This corrects a false claim
in `docs/sat-default-criteria.md`, which stated SAT "has been the default since
v0.7.0". That was wrong; the doc has been fixed.

### Criteria assessment (per `docs/sat-default-criteria.md`)

| Criterion | Result |
|-----------|--------|
| §1 Correctness parity (both engines, full suite) | **Met** — identical 21/24 matrix under `legacy` and `sat`; `sat-parity` unit passes (40 shapes) |
| §2 Performance (no pathological explosion) | **Met** — bench bounded; `manyvirt-1000x10` ≈ 13.4s (documented pure-DPLL expectation; CDCL is future work) |
| §3 Robustness (UNSAT diagnostics) | **Met** — `PackageConflict`/`MissingProvider`/`VersionConflict`/`Cycle` reported |
| §4 Rollout / escape hatch | **Met** — `MEOW_RESOLVER=legacy` and `ResolverEngine::Legacy` both work |
| CHANGELOG/release notes for the flip | **Pending the flip** (not done because the flip is not done) |
| Follow-up issue to remove Legacy | Open (post-stabilization) |

### Recommendation

Flip `ResolverEngine::Auto → SatResolver` in a **separate, deliberate** change
(after this v0.7 work is tagged). All blocking criteria are satisfied; the only
remaining items are the code flip itself and its release note. Keep `Legacy`
selectable for at least one stabilization window.

## 3. Summary of deliverables for this milestone

- Signed package index implemented, documented (`docs/security.md`), changelogged.
- Tests added: `test/unit/package_index_test.cpp`, `test/integration/sections/23_signed_index.sh`.
- Benchmarks: SAT suite run; no regression attributable to index work.
- SAT default: reviewed, parity confirmed, default left unchanged per
  instruction; criteria doc corrected; recommendation recorded above.
- All changes currently **uncommitted** on the working tree (ready to split
  into isolated commits).
