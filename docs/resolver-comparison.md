# Resolver backends: Legacy vs SAT

meow ships two resolution backends behind `ResolverEngine` (config) and the
`MEOW_RESOLVER` environment override.

- **sat** (default since v0.7.0) — the SAT-based `SatResolver`. DPLL over a CNF
  translation. Full support for version constraints, virtual providers, and
  conflict detection. Select via `MEOW_RESOLVER=sat` or `ResolverEngine::Sat`.
  `ResolverEngine::Auto` maps to `Sat`.
- **legacy** — the DFS-based `LegacyResolver` (compatibility mode, default in
  v0.6.x). Provided for debugging and rollback during the transition period.
  Select via `MEOW_RESOLVER=legacy` or `ResolverEngine::Legacy`.

## Correctness parity

40 synthetic fixtures (chains, diamonds, providers, conflicts, version spaces,
optionals, diagnostics) are exercised by `meow-unit-sat-parity`. The SAT backend
matches legacy on all non-virtual scenarios and intentionally diverges on a
subset of provider/virtual cases where SAT provides strictly better results.

### INTENTIONAL DIVERGENCES

| Fixture | Legacy | SAT | Why |
|---------|--------|-----|-----|
| F3 — virtual provider | `client` only | `client + libssl` | SAT resolves virtuals natively |
| F10 — virtual provider | `client` only | `client + libssl` | Same as F3 |
| F14 — conflicting providers | pulls both + core | UNSAT | SAT detects conflict clauses |
| F31 — version constraint `=` | picks highest (3.0) | picks exact (2.0) | SAT enforces version constraints |
| F33 — provider + version constraint | app only | app + highest satisfying provider | SAT resolves versioned virtuals |
| F34 — impossible constraint | SAT (ignores) | UNSAT + VersionConflict | SAT detects impossible constraints |

### Version constraint support

The SAT backend enforces version constraints on dependency edges:

| Constraint | SAT | Legacy |
|------------|-----|--------|
| `= X.Y` | Exact match enforced | Ignored (always highest) |
| `>= X.Y` | At least X.Y | Ignored |
| `<= X.Y` | At most X.Y | Ignored |
| `> X.Y` | Strictly greater | Ignored |
| `< X.Y` | Strictly less | Ignored |
| Combined (`,` separated) | All constraints AND-ed | Ignored |

Per-version SAT variables are created lazily only for packages targeted by
version constraints. The encoding is O(V + E + V_c²) where V_c is the number
of versions of constrained packages (typically 1-5).

### Deterministic provider selection

When a virtual dependency has multiple providers, SAT selects deterministically
by:

1. Highest package version (descending)
2. Package name (alphabetical)

This ensures reproducible installation sets across runs and machines.

### UNSAT diagnostics

When the SAT problem is unsatisfiable, the resolver collects structural
diagnostics without running the solver again:

| Diagnostic kind | Trigger |
|----------------|---------|
| `PackageConflict` | Two conflicting packages both required |
| `MissingProvider` | Virtual dependency with no provider |
| `VersionConflict` | Dependency constraint with no satisfying version |
| `Cycle` | Dependency cycle (from expandInstallRequest) |

## Running both backends

```sh
MEOW_RESOLVER=legacy ./build/meow install app
MEOW_RESOLVER=sat    ./build/meow install app
```

## What to compare

1. **Correctness parity** — `meow-unit-sat-parity` on synthetic graphs.
2. **Translation cost** — `meow-bench` reports SAT pipeline phases.
3. **Scalability** — use the `deep`, `wide`, `many-providers`, `many-versions`,
   `dense-conflicts`, `many-virtuals`, and `random-dag` fixtures.

## Benchmarking

```sh
# SAT pipeline, machine-readable
./build/meow-bench --csv > sat.csv
```

For regression tracking across commits, capture `meow-bench --csv` in CI.
The random-DAG fixtures are seeded for determinism.
