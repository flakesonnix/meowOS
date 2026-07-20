## Goal
Stabilize SAT resolver and refactor integration test suite into CTest-registered standalone sections for future parallel execution.

## Constraints & Preferences
- No CDCL, no watched literals, no VSIDS, no solver optimization beyond current DPLL.
- Do NOT flip SAT to default yet (Legacy remains default via Auto in v0.6.x; flip scheduled for v0.7.0).
- CTest refactor must be pure infrastructure — no behavior changes, no resolver changes, no fixture changes (fixture isolation is Agent B's job).
- `test/integration.sh` must remain working as the legacy sequential entrypoint.
- Each section script must be runnable standalone and as a CTest test, preserving identical assertions and section headers.
- All sections get `RUN_SERIAL TRUE` in CTest until fixture isolation is complete (Agent B).

## Progress
### Done
- **Real repository regression fixtures**: `libbar` (v1.0.0/2.0.0/3.0.0), `myapp` (`libbar>=2.0`), `myapp-exact` (`libbar=1.0`) — committed under `repo/by-name/`, built dynamically in `bootstrapArtifacts`.
- **Integration test suite split** (four commits): Monolithic `test/integration.sh` (~2900 lines) decomposed into 4 module files under `test/integration/`, each in a `run_*_sections()` function. Entry point rewritten as thin 32-line orchestrator. Committed as `e6cd4e3`.
- **Design document**: `docs/parallel-integration-design.md` — section dependency graph, isolation strategy, CTest runner architecture, performance estimate, 4-phase migration plan. Committed as `e0ac0dd`.
- **CI dual-resolver integration confirmed**: `.github/workflows/build.yml` runs both `MEOW_RESOLVER=legacy` and `MEOW_RESOLVER=sat`. Both pass.
- **Documentation**: `docs/resolver-comparison.md` lists SAT first as recommended, Legacy as compatibility mode. `docs/sat-default-criteria.md` rewritten as historical transition record.
- **CTest integration (Phase 1)**: All 22 standalone section scripts created under `test/integration/sections/` (01–22). Each has a `run_section()` function and `BASH_SOURCE` guard for standalone execution.

### In Progress
- **Legacy runner rewrite**: `test/integration.sh` must be rewritten as a thin runner that sources all 22 section files and calls `run_section` in original order.
- **Old module cleanup**: Delete `install.sh`, `repository.sh`, `package_features.sh`, `resolver.sh` from `test/integration/`.
- **CMakeLists.txt registration**: `add_test()` with `RUN_SERIAL TRUE` for each section.
- **Docs update**: `docs/testing.md` with CTest layout, labels, and usage.

### Blocked
- (none)

## Key Decisions
- **CTest over custom scheduler**: Using CMake's `add_test()` and `ctest -jN` instead of writing a bash worker pool. Both inherit the existing CMake build system and provide timeouts, labels, and XML output for free.
- **BASH_SOURCE guard pattern**: Each section script defines `run_section()` and detects standalone execution via `[[ "${BASH_SOURCE[0]}" == "${0}" ]]`. When sourced by the legacy runner, only the function is defined; when run directly, it bootstraps + runs + reports.
- **RUN_SERIAL until fixture isolation**: All integration CTest tests marked `RUN_SERIAL TRUE` initially. Fixture isolation (per-test repo copies, temp HOME, cache isolation) deferred to Agent B.
- **Reorder accepted within modules**: Sections are grouped by topic within each module file, not by exact original section number order. The 4 module functions are called in a logical (not literal original) order from the entry point.

## Next Steps
1. Rewrite `test/integration.sh` as legacy sequential runner that sources all 22 section files and calls `run_section` in original order.
2. Delete old module files (`install.sh`, `repository.sh`, `package_features.sh`, `resolver.sh`).
3. Register all sections in root `CMakeLists.txt` via `file(GLOB ...) + add_test(...)` with `RUN_SERIAL TRUE`.
4. Update `docs/testing.md` with CTest layout, labels, and usage.
5. Verify: `./test/integration.sh` (legacy), `ctest`, `ctest -j8` all pass.
6. Cut v0.6.1 with CTest infrastructure in place (parallelism still serial until Agent B's fixture isolation).

## Critical Context
- **SAT vs Legacy semantic gap**: Version constraints, virtual provider expansion, and conflict detection are ONLY in SAT. Legacy ignores them. Documentation reflects this.
- **python3 is NOT available in the test environment** (missing from PATH). Sections 11d, 16–17, 23, 25–29, 34–35 require python3 and will fail until it's installed. Pre-existing issue, not a regression.
- **TMPDIR is unset by default** (optional system variable). Fixed in `common.sh` with `: "${TMPDIR:=/tmp}"`.
- **Existing duplicate section numbers** (two section 31s, two 32s) are preserved as-is.
- **Global state shared across sections**: `pass`/`fail` counters, `$TEST_DB`, `$MEOW`, `$HTTP_PID` are all global. Each section script expects its own process (standalone) or inherited state (legacy runner).
- **Type change**: `RepositoryPackage.depends` is now `vector<Dependency>` (not `vector<PackageName>`). All call sites use `dep.name.value` instead of `dep.value`.

## Relevant Files
- `test/integration.sh`: Legacy sequential entry point (to be rewritten to source 22 section scripts).
- `test/integration/common.sh`: Shared helpers (`check`, `buildPkg`, `reproBuild`, `registerHookPkg`, `registerOptPkg`, `bootstrapArtifacts`, `startHttp`, `stopHttp`).
- `test/integration/sections/01_basic_install.sh` through `22_groups.sh`: All 22 standalone section scripts (complete).
- `test/integration/install.sh`, `repository.sh`, `package_features.sh`, `resolver.sh`: Old module files (to be deleted after section scripts are verified).
- `CMakeLists.txt`: Root CMake file — needs `enable_testing()` and `add_test()` blocks for unit tests and integration sections.
- `docs/parallel-integration-design.md`: Isolation strategy, dependency graph, performance estimate, migration plan.
- `docs/resolver-comparison.md`, `docs/sat-default-criteria.md`: Resolver documentation.
- `.github/workflows/build.yml`: Dual-resolver CI.
