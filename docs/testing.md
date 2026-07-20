# Testing

## Directory layout

```
test/
├── integration.sh              # Legacy sequential runner (sources all sections)
├── integration/
│   ├── common.sh               # Shared helpers (bootstrap, repo fixtures, HTTP server)
│   └── sections/
│       ├── 01_basic_install.sh  # Sections 1–10
│       ├── 02_repo_metadata.sh  # Sections 11–14
│       ├── 03_download.sh       # Section 15
│       ├── 04_http.sh           # Sections 16–17
│       ├── 05_doctor.sh         # Sections 18, 21
│       ├── 06_reproducible.sh   # Sections 19, 24
│       ├── 07_hooks.sh          # Section 20
│       ├── 08_fresh_install.sh  # Sections 22–23
│       ├── 09_server_hosting.sh # Section 25
│       ├── 10_http_backend.sh   # Section 26
│       ├── 11_multi_repo.sh     # Section 27
│       ├── 12_doctor_repo.sh    # Section 28
│       ├── 13_dual_backend.sh   # Section 29
│       ├── 14_resolver.sh       # Sections 30–32
│       ├── 15_health_state.sh   # Section 31
│       ├── 16_priority.sh       # Section 32
│       ├── 17_mirror_groups.sh  # Section 33
│       ├── 18_mirror_failover.sh# Section 34
│       ├── 19_parallel_refresh.sh# Section 35
│       ├── 20_history.sh        # Section 36
│       ├── 21_optionals.sh      # Section 37
│       └── 22_groups.sh         # Package groups
├── unit/                        # C++ unit tests (disk/network-free)
└── keys/                        # Signing keys for repo verification
```

## Required tools

The integration suite needs the package-manager binaries (`build/meow`,
`build/meow-build`, `build/meow-repo`, `build/meow-server`) plus a few external
tools. Sections that require a missing tool are **skipped** (reported as
`SKIP`, not `FAIL`) instead of failing, so an incomplete environment does not
produce false regressions.

| Tool       | Used by                                                        | Missing → behavior |
|------------|----------------------------------------------------------------|--------------------|
| `python3`  | HTTP fixture server, free-port selection (sections 02, 04, 08, 09, 10, 11, 12, 13, 18, 19), sqlite3 schema fixtures (02, 08) | section SKIPs |
| `curl`     | HTTP transport / server assertions (09, 10, 11, 12, 13, 18, 19) | section SKIPs |
| `tar`      | archive inspection (06)                                        | required; abort |
| `zstd`     | archive compression (build + 06)                               | required; abort |
| `git`      | repo reset after run (`integration.sh`)                        | required; abort |
| `rsync`    | alternative fixture copy strategy (`FIXTURE_COPY=rsync -a`)    | optional; falls back to `cp -a` |

`python3` is **not mandatory** for the core suite: sections that need it skip
gracefully. For full HTTP/transport coverage install `python3` and `curl`. CI
must provide both `python3` and `curl` so the HTTP sections are exercised
rather than skipped (see `.github/workflows/build.yml`).

## Skipping

When a section bails out due to a missing tool, the runner prints:

```
  SKIP: missing required tool(s): python3
```

and the final summary reports a skip count:

```
Results: 92 passed, 0 failed, 10 skipped
```

A non-zero failure count still exits non-zero; skips do not.

## Running tests

### Legacy sequential runner
```bash
MEOW_RESOLVER=sat ./test/integration.sh
MEOW_RESOLVER=legacy ./test/integration.sh
```

### Single section standalone
```bash
MEOW_RESOLVER=sat ./test/integration/sections/14_resolver.sh
```

### CTest (all registered tests)
```bash
# configure (once)
cmake -B build -S .

# run all tests
ctest --output-on-failure

# run only integration sections
ctest -L integration --output-on-failure

# run a single section
ctest -R integration.14.resolver --output-on-failure

# parallel (sections are RUN_SERIAL currently — no speedup yet)
ctest -j8 --output-on-failure
```

## CTest test names

| Name                          | Label(s)              | RUN_SERIAL |
|-------------------------------|-----------------------|------------|
| `unit.*`                      | unit                  | no         |
| `integration.XX.name`         | integration, serial   | yes        |
| `integration.legacy`          | integration, serial   | yes        |

## Labels

- **unit** — C++ unit tests (no network, no disk fixtures).
- **integration** — bash integration tests that exercise real package repositories.
- **serial** — tests that share global state and must not run concurrently. All integration sections are serial until fixture isolation is complete.

## Future parallelization plan

See `docs/parallel-integration-design.md` for the full migration plan (4 phases). In short:

1. Phase 1 (current): All sections standalone + CTest registration. `RUN_SERIAL TRUE`.
2. Phase 2: Fixture isolation (per-test repo copies, temp HOME, cache isolation).
3. Phase 3: Remove `RUN_SERIAL`, switch to `RESOURCE_LOCK` or label-based batching.
4. Phase 4: Optional NUMA-aware scheduling for HTTP + crypto-heavy sections.
