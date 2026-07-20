# Criteria for making SAT the default resolver

`SatResolver` is feature-complete but remains off by default (`ResolverEngine::Auto`
maps to `Legacy`). Before flipping the default, the following criteria must be
satisfied. None of these require changing resolver *semantics* — they are
verification, performance, and rollout gates.

## 1. Correctness parity (blocking)

- [ ] `meow-unit-sat-parity` passes on all synthetic graph shapes
      (chains, diamonds, providers, conflicts, version spaces).
- [ ] The full integration suite (`test/integration.sh`) passes **identically**
      under `MEOW_RESOLVER=legacy` and `MEOW_RESOLVER=sat`. CI already runs both;
      this gate is "green on both, zero diff in selected packages".
- [ ] Parity holds for the harder semantics exercised by integration tests:
      repository priority, conflicts, virtual provides, optional dependencies,
      and package groups.
- [ ] No behavior change in install/remove/upgrade transaction outcomes.

## 2. Performance (blocking)

- [ ] For representative repositories, SAT `total` (scan+graph+vars+clauses+
      solve+map) is within an agreed bound of the legacy resolver's end-to-end
      time (target: no worse than legacy on the shipped fixtures; better on
      wide / many-version / dense-conflict shapes).
- [ ] `meow-bench` shows no pathological clause explosion on the `dense-conflicts`
      and `many-versions` fixtures that would make SAT slower than legacy.
- [ ] Solve time is bounded and deterministic for the seeded `random-dag`
      fixtures.

## 3. Robustness (blocking)

- [ ] Unsatisfiable requests (conflicts, missing deps) are reported with the
      same user-visible error categories as legacy (e.g. `Unsatisfiable`,
      `DependencyNotFound`), not a raw solver failure.
- [ ] Memory use scales with repository size comparably to legacy; no unbounded
      growth on the large fixtures (`deep-50000`, `dense-500`).

## 4. Rollout / escape hatch (blocking)

- [ ] The `MEOW_RESOLVER` override and `ResolverEngine` config continue to work
      so operators can pin `legacy` if a regression appears post-flip.
- [ ] CHANGELOG + release notes document the switch and the rollback path.
- [ ] A follow-up issue is filed to remove the legacy backend once SAT has
      shipped as default for a stabilization window (e.g. one release cycle).

## 5. Documentation (non-blocking)

- [ ] `docs/benchmark-methodology.md` and `docs/resolver-comparison.md` are
      current (done).
- [ ] User docs note which resolver is default and how to override it.

## Flip procedure

When all blocking boxes are checked:

1. Change `ResolverEngine::Auto` handling in
   `meow/src/dependency/resolver_factory.cpp` so `Auto` returns `SatResolver`.
2. Update the config doc comment in `meow/include/meow/config/config.hpp`.
3. Bump CHANGELOG; cut release notes.
4. Keep `legacy` selectable via config/env for the stabilization window.
