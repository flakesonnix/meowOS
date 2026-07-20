# Parallel Integration Test Suite — Design

## Goal
Refactor the integration test suite (30+ sections, ~2700 lines of bash) into independently runnable sections that can execute in parallel, while preserving deterministic output and sequential compatibility.

---

## 1. Section Dependency Graph

### Legend
- `R` — reads shared `repo/` (filesystem repository)
- `M` — mutates shared `repo/` (modifies files, restores them)
- `T` — creates its own temporary repo tree (fully isolated)
- `H` — needs HTTP fixture server
- `K` — reads/mutates `~/.config/meow/keys`
- `C` — reads/mutates `~/.cache/meow`
- `D` — uses shared `$TEST_DB`
- `O` — uses its own database
- `*` — standalone binary, no integration infrastructure

| Section | Content | Reads | Mutates | Own DB | HTTP | Independent? |
|---------|---------|-------|---------|--------|------|-------------|
| 1–10 | Basic install/remove/verify | R, D, C | — | — | — | **Yes** (with fixture copy) |
| 11 | Format version rejection | R, D | M (repo/) | — | — | **Sequential** (mutates repo/) |
| 12 | Key trust | R, D, K | M (repo/, keys/) | — | — | **Sequential** (mutates repo/ + keys) |
| 13 | Metadata expiry | R, D | M (repo/) | — | — | **Sequential** (mutates repo/) |
| 14 | Repository identity | R, D | M (repo/) | — | — | **Sequential** (mutates repo/) |
| 15 | Download robustness | R, D, C | M (version files) | — | — | **Sequential** (mutates repo/) |
| 16 | HTTP transport | R, C | M (version files) | — | H | **Sequential** (mutates repo/) |
| 17 | Parallel downloads | R, C | M (version files) | — | H | **Sequential** (mutates repo/) |
| 18 | Doctor diagnostics | D | — | — | — | **Yes** |
| 19 | Reproducible builds | — | — | O | — | **Yes** (fully isolated) |
| 20 | Restricted hooks | R | M (registerHookPkg) | — | — | **Sequential** (mutates repo/) |
| 21 | Doctor --security | D, C | — | — | — | **Yes** |
| 22 | Fresh install | R | — | O | — | **Yes** |
| 23 | DB migration (v0.3→v0.4) | — | — | O | — | **Yes** (no repo) |
| 24 | Reproducibility release | — | — | O | — | **Yes** (fully isolated) |
| 25 | meow-server hosting | — | T | O | H | **Yes** (own repo) |
| 26 | HTTP backend | — | T | O | H | **Yes** (own repo) |
| 27 | Multiple repos | — | T | O | H | **Yes** (own repo) |
| 28 | Doctor per-repo | — | T | O | H | **Yes** (own repo) |
| 29 | Dual-backend parity | — | T | O | H | **Yes** (own repo) |
| 30 | Resolver regression | R | — | O | — | **Yes** |
| 31 | Repository health | — | T | O | — | **Yes** (own repos) |
| 32 | Priority selection | — | T | O | — | **Yes** (own repos) |
| 33 | Mirror groups | — | T | O | — | **Yes** (own repos) |
| 34 | Mirror failover | — | T | O | H | **Yes** (own repos + HTTP) |
| 35 | Parallel refresh | — | T | O | H | **Yes** (own repos + HTTP) |
| 36 | Package history | R | — | O | — | **Yes** |
| 37 | Optional deps | R | M (registerOptPkg) | O | — | **Sequential** (mutates repo/) |
| groups | Package groups | R | — | O | — | **Yes** |
| unit-* | Standalone binaries | * | * | * | — | **Yes** (fully isolated) |

### Dependency Graph (textual)

```
[bootstrap] ──┬──► 1-10 (basic install)
              ├──► 11-14 (repo metadata) ──mutates repo/──► [restore repo]
              ├──► 15 (download) ──mutates repo/──► [restore]
              ├──► 16-17 (HTTP) ──mutates repo/──► [restore]
              ├──► 18 (doctor)     ──reads TEST_DB──
              ├──► 19 (repro build)──no deps──
              ├──► 20 (hooks) ──mutates repo/──
              ├──► 21 (security)   ──reads TEST_DB──
              ├──► 22 (fresh)      ──reads repo/──
              ├──► 23 (DB migrate) ──no deps──
              ├──► 24 (repro release)──no deps──
              ├──► 25-29 (HTTP/repo)──own repos──
              ├──► 30 (resolver)   ──reads repo/──
              ├──► 31-35 (mirrors) ──own repos──
              ├──► 36 (history)    ──reads repo/──
              ├──► 37 (optionals)  ──mutates repo/──
              ├──► groups          ──reads repo/──
              └──► unit-*          ──no deps──
```

**Key insight**: Only sections 11–17, 20, and 37 mutate the shared `repo/` tree. Everything else is read-only or creates its own isolated repos. A frozen snapshot of `repo/` eliminates the sequential bottleneck.

---

## 2. Isolation Strategy

### Per-Section Isolation Plan

**Shared fixture `repo/` must be frozen (read-only copy) before any parallel execution.** Sections that mutate it either need the original writable `repo/` (sequential group) or need their own writable copy.

| Resource | Isolation mechanism |
|----------|-------------------|
| Repository (`repo/`) | Snapshot → `repo.frozen/` (read-only bind mount or `cp -r`). Parallel sections read from frozen copy. Sequential sections get exclusive write access. |
| Database | Each section creates its own via `--db-path /tmp/meow-{section}-$$.db`. No shared `$TEST_DB`. |
| Cache (`~/.cache/meow`) | Override via `MEOW_CACHE_DIR` env var → per-section temp dir. |
| Config (`~/.config/meow/keys`) | Override via `HOME` → per-section temp dir, seeded with key. |
| Tempdir (`/tmp/meow-*`) | Each section uses `${SECTION_TMPDIR}`. |
| HTTP servers | Each HTTP-dependent section owns its server process (PID tracked, killed on exit). Ports assigned via OS (`port=0`). |

### Env Var Interface

Sections should consume these environment variables instead of hardcoded paths:

```bash
# Set by runner per worker:
SECTION_WORKDIR="/tmp/meow-worker-3"      # unique per worker
SECTION_HOME="$SECTION_WORKDIR/home"
SECTION_CACHE="$SECTION_WORKDIR/cache"
SECTION_REPO="$SECTION_WORKDIR/repo"       # frozen copy of repo/
SECTION_DB="$SECTION_WORKDIR/test.db"
```

### Sections Needing Special Handling

**HTTP-dependent (25–29, 34–35)**: These already create their own repos, configs, and databases. They also spawn their own `meow-server` instances on ephemeral ports. Isolation is near-total — only port conflicts are a concern (unlikely with OS-assigned ports).

**Hook runner (20) and Optionals (37)**: Call `registerHookPkg`/`registerOptPkg` which mutate `repo/`. Must run in the sequential group or get a writable snapshot.

**Fresh install (22)**: Already uses `FRESH_DB` (own DB). Still reads `repo/` — needs the frozen copy.

---

## 3. Classification

### Group A: Parallel-Safe (no changes needed beyond env isolation)
| Sections | Justification |
|----------|---------------|
| 19 | No repo, no DB, no HTTP — pure file build tests |
| 22 | Own DB, reads repo/ read-only (needs frozen copy) |
| 23 | No repo, no HTTP — python+DB only |
| 24 | Same as 19 — pure reproducibility |
| 25 | Own server + own fixture tree |
| 26 | Own repo-http + own server |
| 27 | Own repo-http + own server |
| 28 | Own repo-http + own server |
| 29 | Own repo-dual (copy of repo/) + own server |
| 30 | Own DB, reads repo/ read-only (needs frozen copy) |
| 31 | Own repos (repo-expired, repo-badsig, repo-badmeta) |
| 32 | Own prio-* repos, own DB |
| 33 | Own mir-* repos, own DB |
| 34 | Own fo-* repos, own servers, own DB |
| 35 | Own servers, own repos, own DB |
| 36 | Own DB + own config, reads repo/ read-only |
| groups | Own DB + own config, reads repo/ read-only |
| unit-* | Standalone binaries, no integration infra |

### Group B: Sequential Only — Mutates Shared `repo/`
| Sections | Reason |
|----------|--------|
| 11 | Modifies `repo/by-name/he/hello/package.toml` and `repo/repository.toml` |
| 12 | Moves key file, modifies `repo/repository.toml` |
| 13 | Modifies `repo/repository.toml` and re-signs |
| 14 | Modifies `repo/repository.toml` and re-signs |
| 15 | Modifies `hello/versions/1.1.0.toml` |
| 16 | Modifies `app/versions/1.0.0.toml`, needs HTTP |
| 17 | Modifies multiple version files, needs HTTP |
| 20 | Adds hook packages via `registerHookPkg` |
| 37 | Adds optional packages via `registerOptPkg` |

### Group C: Need shared DB (1–10, 18, 21)
These read `$TEST_DB` which was populated by section 2 (install chain). If we give each section its own DB, sections 1,3-10,18,21 need the DB to be pre-populated by installing `app`. They are **not independent** of section 2.

Solution: Either run 1–10 as a single sequential unit, or have each sub-section bootstrap its own DB. The simplest approach: keep 1–10 as an atomic block. Sections 18 and 21 similarly rely on the DB state from 1–10.

**Updated classification**: 1–10 is a single sequential block. 18 and 21 depend on the DB from 1–10.

---

## 4. Runner Architecture

### Option A: CTest (recommended)

```cmake
# CMakeLists.txt
enable_testing()

# Unit tests (existing)
add_test(NAME unit-backend COMMAND meow-unit-backend)
add_test(NAME unit-history COMMAND meow-unit-history)

# Integration sections registered as individual CTest tests
file(GLOB INTEGRATION_SECTIONS test/integration/sections/*.sh)
foreach(SECTION ${INTEGRATION_SECTIONS})
    get_filename_component(NAME ${SECTION} NAME_WE)
    add_test(NAME integration.${NAME}
        COMMAND bash ${SECTION}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(integration.${NAME} PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "MEOW_RESOLVER=${MEOW_RESOLVER}")
endforeach()
```

Then: `ctest -j8` for parallel, `ctest --output-on-failure` for CI.

**Pros**: Zero custom scheduler code. Timeouts, labels, XML reports, GitHub Actions integration built in. Already used for unit tests.
**Cons**: CMake dependency (already satisfied). Output ordering is not in section number order by default (CTest groups by test completion).

### Option B: Custom Bash Worker Pool

```bash
# run.sh — lightweight coordinator
WORKERS=${WORKERS:-$(nproc)}
PENDING=(sections/*.sh)
RESULTS=()

run_worker() {
    local section=$1
    SECTION_TMPDIR=$(mktemp -d)
    cp -r repo.frozen "$SECTION_TMPDIR/repo"
    HOME="$SECTION_TMPDIR/home" \
    MEOW_CACHE_DIR="$SECTION_TMPDIR/cache" \
    bash "$section" 2>&1
    echo "EXIT:$?" >> "$SECTION_TMPDIR/result"
}
```

Output ordering: collect results, sort by section number, print in order after all complete.

### Recommended: CTest

Using CTest avoids maintaining a custom scheduler. The sections directory can be flat `.sh` files, each independently runnable. CI runs:

```yaml
- run: ctest -j8 --output-on-failure
```

instead of:

```yaml
- run: ./test/integration.sh
```

Both can coexist — the sequential runner can source the same section files in order.

---

## 5. Performance Estimate

Assuming wall-clock baseline from current sequential run:

| Section group | Est. time (s) | Notes |
|--------------|--------------|-------|
| 1–10 (basic install) | 8 | Sequential atomic block |
| 11–17 (repo metadata + HTTP) | 25 | Sequential (mutates repo/) |
| 18–24 (features + fresh) | 15 | Mixed, mostly parallelizable |
| 25–29 (HTTP repos) | 30 | Fully parallel (own fixtures) |
| 30 (resolver) | 3 | Short |
| 31–35 (mirrors/priority) | 25 | Fully parallel (own fixtures) |
| 36–37 + groups (history/optional) | 10 | Seq (mutation) + parallel |
| Unit test binaries | 2 | Trivial |

**Estimated sequential total**: ~118s

### Parallel Speedup

With frozen `repo/` and isolated state, the dependency chain is:

```
[bootstrap = 12s] → [1-10 = 8s] → [11-17 = 25s] → [18-24 = 15s] → [rest = 70s]
                                        ↑
                                  (sequential bottleneck)
```

All of 25–29, 31–35, unit tests, 30, groups, 36, 19, 22, 23, 24 can run in parallel with 1–10 and 11–17.

| Workers | Critical path | Speedup |
|---------|--------------|---------|
| 1 (seq) | 12 + 8 + 25 + 15 + 70 = **130s** | 1.0× |
| 2 | 12 + max(8, 25) + 15 + 70 = **122s** | ~1.07× |
| 4 | 12 + max(8, 25) + max(15, 70) = **107s** | ~1.21× |
| 8 | 12 + 25 + 70 = **107s** | ~1.21× |

**The 11-17 sequential block is the bottleneck** (25s wall time). Even with infinite workers, the critical path is ~107s. True speedup is limited to ~1.2× because ~80% of the suite is already sequential (installing packages, waiting for HTTP timeouts).

If 11-17 can be split further (they share `repo/` but could get writable snapshots), the bottleneck shrinks:

| Workers | Optimistic critical path | Speedup |
|---------|------------------------|---------|
| 8 | 12 + max(8, 2, 5, 5, 3, 3, ...) + 70 = **85s** | ~1.5× |

The real value of parallelization is not wall-clock speed but **developer iteration**: failing sections can be re-run individually in seconds instead of minutes.

---

## 6. Migration Plan

### Phase 1: Split into Standalone Section Files (this commit)

```
test/integration/
    common.sh          # shared helpers (sourced by each section)
    run.sh             # sequential runner (sources sections in order)
    sections/
        01_install.sh
        02_repository.sh
        03_download.sh
        04_http.sh
        ...
        18_doctor.sh
        19_reproducible_build.sh
        ...
        zz_units.sh
```

Each section file is a standalone script (no function wrapper):

```bash
#!/usr/bin/env bash
set -euo pipefail
. "$(cd "$(dirname "$0")/.." && pwd)/common.sh"
# ... test code ...
```

`run.sh` sources them in numbered order for sequential execution.

**Duration**: 1 session. **Risk**: Low (pure mechanical split).

### Phase 2: Isolate State

- `bootstrapArtifacts` → outputs to a deterministic staging dir
- Each section gets: `$SECTION_TMPDIR`, `$SECTION_DB`, `$SECTION_HOME`, `$SECTION_REPO`
- Introduce `repo.frozen` (read-only snapshot created once after bootstrap)
- Make all HTTP sections use ephemeral ports (already done)
- Add env-var overrides for cache, home, repo path

**Duration**: 2 sessions. **Risk**: Medium (some sections have implicit state dependencies).

### Phase 3: Enable Parallelism (CTest)

- Register each section file as a CTest test
- Add `--jobs N` to CI invocation
- Update `run.sh` to support `--parallel` flag

**Duration**: 1 session. **Risk**: Low (CTest handles scheduling).

### Phase 4: CI Tuning

- Add parallel CI matrix (sequential vs parallel)
- Set per-test timeouts
- Add test labels for filtering (e.g., `--label-regex http`)

**Duration**: 1 session. **Risk**: Low.

---

## Appendix: Section File Layout (draft)

```
test/integration/
    common.sh               # Shared: check(), buildPkg(), bootstrapArtifacts(), etc.
    run.sh                  # Sequential coordinator (sources sections in order)
    sections/
        01_basic_install.sh     # §§1-10  — basic install/remove/verify chain
        02_repo_metadata.sh     # §§11-14 — format rejection, keys, expiry, identity
        03_download.sh          # §15     — download robustness
        04_http_transport.sh    # §16     — HTTP transport (needs server)
        05_parallel_dl.sh       # §17     — parallel downloads (needs server)
        06_doctor.sh            # §18,21  — doctor diagnostics + security
        07_reproducible.sh      # §19,24  — reproducibility (build + release)
        08_hooks.sh             # §20     — restricted hook runner
        09_fresh_install.sh     # §22     — fresh install from empty
        10_db_migration.sh      # §23     — v0.3→v0.4 DB migration
        11_server_hosting.sh    # §25     — meow-server
        12_http_backend.sh      # §26     — HTTP repository backend
        13_multi_repo.sh        # §27     — multiple repositories
        14_doctor_repo.sh       # §28     — doctor per-repository
        15_dual_backend.sh      # §29     — filesystem vs HTTP parity
        16_resolver.sh          # §30     — version constraint regression
        17_health_state.sh      # §31     — repository health
        18_priority.sh          # §32     — repository priority
        19_mirror_groups.sh     # §33     — mirror groups
        20_mirror_failover.sh   # §34     — mirror failover
        21_parallel_refresh.sh  # §35     — parallel refresh
        22_history.sh           # §36     — package history
        23_optionals.sh         # §37     — optional dependencies
        24_groups.sh            # groups  — package groups
        25_unit_tests.sh        # unit-*  — standalone test binaries
```
