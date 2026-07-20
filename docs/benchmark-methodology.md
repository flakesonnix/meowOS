# Benchmark methodology

`meow-bench` is a disk/network-free micro-benchmark for the SAT resolution
pipeline. Repositories are generated in memory from synthetic shapes so runs are
reproducible and isolated from I/O.

## Running

```sh
nix develop --command bash -c 'cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j'
./build/meow-bench            # human-readable table
./build/meow-bench --csv      # machine-readable CSV on stdout
```

## Measured phases

The translation pipeline is split into independently timed stages so cost is
attributed precisely rather than hidden behind a single opaque number.

| Phase             | What it measures                                   |
|-------------------|----------------------------------------------------|
| repository scan   | One pass building the name→index map (O(V)).       |
| graph build       | Forward/reverse adjacency + conflict lists (O(V+E)).|
| variable assign   | Declaring a variable per package / provide name.    |
| clause generation | Emitting all CNF clauses from adjacency (O(V+E)).   |
| SAT solving       | DPLL search over the generated problem.             |
| assignment mapping| Reconstructing the selected package set from the assignment. |
| total             | End-to-end wall time across all of the above.       |

Timing is done with `std::chrono::steady_clock` at microsecond resolution. The
first six phases are reported as milliseconds; `total` is wall-clock end-to-end.

## Fixtures

| Fixture family     | Shape                                                        |
|--------------------|-------------------------------------------------------------|
| deep chain         | Linear `p0 → p1 → … → p(n-1)` dependency chain.             |
| wide graph         | One root → `w` mid packages → `w²` leaves.                  |
| many providers     | One virtual `service` provided by `k` packages.             |
| many versions      | `p` packages each with `v` versions, chained by dependency. |
| dense conflicts    | `n` packages where every pair conflicts (O(n²) clauses).    |
| random DAG         | Seeded `mt19937_64` DAG; same (nodes, prob, seed) ⇒ same graph. |

The random DAG uses a fixed seed (`42`) so a given `(nodes, edgeProb, seed)`
triple always produces the identical graph — benchmarks are reproducible across
machines and CI runs.

## Output

- **Table** (default): one row per fixture with package/var/clause counts and
  per-phase timings.
- **CSV** (`--csv`): header row + one row per fixture, suitable for
  `benchstat`, spreadsheets, or diffing between resolver backends.

## Caveats

- Times are single-shot (no warm-up, no averaging). Use multiple CI runs or
  `benchstat` aggregation for regression detection.
- The pipeline uses the DPLL solver; CDCL / watched-literal optimizations are
  not yet measured here and would change the `SAT solving` column only.
- Fixtures are synthetic. Real-world repositories may have different
  dependency and conflict density.
