# Comparing resolver performance

meow ships two resolution backends behind `ResolverEngine` (config) and the
`MEOW_RESOLVER` environment override:

- **legacy** — the DFS-based `LegacyResolver` (current default via `Auto`).
- **sat** — the SAT-based `SatResolver` (DPLL over a CNF translation).

## Running both backends

The integration suite and the CLI both honor `MEOW_RESOLVER`:

```sh
MEOW_RESOLVER=legacy ./build/meow install app
MEOW_RESOLVER=sat    ./build/meow install app
```

CI runs the full integration suite once per backend (see `.github/workflows/build.yml`);
a failure is reported with the resolver name in the step title so regressions
are attributable immediately.

## What to compare

1. **Correctness parity** — `SatResolver` and `LegacyResolver` must select the
   same package set. The unit test `meow-unit-sat-parity` encodes this contract
   on synthetic graphs; the CI split runs the same integration scenarios
   through both backends to catch divergence on real repository semantics
   (priority, conflicts, provides, optional deps).

2. **Translation cost** — `meow-bench` reports the SAT pipeline phases
   (repository scan, graph build, variable assignment, clause generation, SAT
   solving, assignment mapping, total). The legacy resolver has no equivalent
   translation step, so compare legacy *end-to-end install time* against the
   SAT `total` column for a like-for-like wall-clock view.

3. **Scalability** — use the `deep`, `wide`, `many-versions`, `dense-conflicts`,
   and `random-dag` fixtures to see how each backend behaves as the graph grows.
   The SAT phases expose where cost lives (clause explosion vs. solve time);
   the legacy resolver's cost is implicit in its recursion depth.

## Producing comparable numbers

```sh
# SAT pipeline, machine-readable
./build/meow-bench --csv > sat.csv

# Legacy end-to-end, timed around the CLI
MEOW_RESOLVER=legacy /usr/bin/time -v ./build/meow install app
```

For regression tracking across commits, capture `meow-bench --csv` in CI and
diff with `benchstat` or a simple CSV diff. The random-DAG fixtures are seeded
for determinism, so two runs of the same binary are directly comparable.
