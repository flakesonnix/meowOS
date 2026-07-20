# SAT-as-default resolver — transition criteria

`SatResolver` is feature-complete and has been the default since v0.7.0
(`ResolverEngine::Auto` maps to `Sat`). `Legacy` remains selectable via
`MEOW_RESOLVER=legacy` or `ResolverEngine::Legacy` for compatibility.

The following criteria were used to gate the flip:

## 1. Correctness parity (blocking)

- [x] `meow-unit-sat-parity` passes on all 40 synthetic graph shapes
      (chains, diamonds, providers, conflicts, version spaces, optionals,
      diagnostics, UNSAT scenarios).
- [x] The full integration suite (`test/integration.sh`) passes under both
      `MEOW_RESOLVER=legacy` and `MEOW_RESOLVER=sat`. CI runs both (see
      `.github/workflows/build.yml`). Intentional divergences (version
      constraints, virtual providers) are documented in the divergence table.
- [x] Parity holds for the harder semantics exercised by integration tests:
      repository priority, conflicts, virtual provides, optional dependencies,
      and package groups — identical outcomes where semantics overlap;
      documented divergences where SAT is strictly better.
- [x] No regressions in install/remove/upgrade transaction outcomes — all
      fixtures pass under both resolvers.

### Known intentional divergences

| Scenario | Legacy | SAT | Rationale |
|----------|--------|-----|-----------|
| Virtual provider expansion | Not expanded | Provider selected | SAT-native virtual resolution |
| Conflicting providers | Both pulled | UNSAT | Conflict clause detection |
| Version constraints | Ignored | Enforced | `=`, `>=`, `<=`, `>`, `<` |
| Impossible constraints | Silent success | UNSAT + diagnostic | VersionConflict diagnostic |
| Deterministic provider | Implicit (DFS order) | Explicit (version desc, name asc) | Reproducible builds |

## 2. Performance (blocking)

- [x] For representative repositories, SAT `total` (scan+graph+vars+clauses+
      solve+map) is within an agreed bound of the legacy resolver's end-to-end
      time. SAT is faster on wide, many-version, and dense-conflict shapes;
      comparable on linear chains. See `meow-bench --csv` for per-phase
      breakdown.
- [x] `meow-bench` shows no pathological clause explosion on the `dense-conflicts`,
      `many-versions`, or `many-virtuals` fixtures.
- [x] Solve time is bounded and deterministic for the seeded fixtures.
      The largest fixture (`manyvirt-1000x10`: 11001 vars, 13001 clauses)
      solves in ~15s with pure DPLL (expected; CDCL will improve).

## 3. Robustness (blocking)

- [x] Unsatisfiable requests (conflicts, missing providers, impossible version
      constraints) are reported with structured `ResolveDiagnostic` entries:
      `PackageConflict`, `MissingProvider`, `VersionConflict`, `Cycle`.
- [x] Memory use scales with repository size comparably to legacy.
      No unbounded growth on large fixtures (`deep-50000`, `dense-500`).

## 4. Rollout / escape hatch (blocking)

- [x] The `MEOW_RESOLVER` override and `ResolverEngine` config continue to work
      so operators can pin `legacy` if a regression appears post-flip.
- [ ] CHANGELOG + release notes document the switch and the rollback path
      (before v0.7.0 release).
- [ ] A follow-up issue is filed to remove the legacy backend once SAT has
      shipped as default for a stabilization window (e.g. one release cycle).

## 5. Documentation (non-blocking)

- [x] `docs/resolver-comparison.md` documents constraints, divergences,
      diagnostics, and provider selection policy.
- [x] `docs/benchmark-methodology.md` and `docs/resolver-comparison.md` are
      current.
- [x] `docs/resolver-comparison.md` marks SAT as recommended, Legacy as
      compatibility mode. The `MEOW_RESOLVER` env var and `ResolverEngine`
      config options are documented.

## Flip log

| Step | Date | Commit |
|------|------|--------|
| `ResolverEngine::Auto` → `SatResolver` | v0.7.0 | — |
| Docs reflect SAT as default | v0.7.0 | — |
| Legacy stays selectable via config/env | v0.7.0 | — |
| Follow-up issue filed to remove Legacy | v0.8.x | — |
