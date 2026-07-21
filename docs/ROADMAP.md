# MeowOS Roadmap

## Phase 0 - Design

Goals:

- Define architecture
- Define package format
- Define database model
- Define repository structure


## Phase 1 - Local Package Manager

Goal:

Install local packages.

Features:

- Read package.toml
- Open tar.xz packages
- Extract files
- Track installed files
- SQLite database

Commands:


meow install package.tar.xz
meow remove package
meow list
meow info package



## Phase 2 - Dependency Resolver

Features:

- Dependency graph
- Dependency ordering
- Missing dependency detection
- Conflict detection


## Phase 3 - Repository System

Features:

- Remote repositories
- Package indexes
- Searching packages
- Synchronizing repositories


## Phase 4 - Security

Features:

- Package signatures
- Repository signatures
- Hash verification


## Phase 5 - Builder

Features:

- Read recipes
- Download sources
- Verify sources
- Build software
- Generate packages


## Phase 6 - Bootstrap

Features:

- Create root filesystem
- Build base packages
- Create usable system


## Phase 7 - Bootable System

Features:

- Linux kernel
- OpenRC
- Initramfs
- Bootloader
- Login


## Phase 8 - Desktop

Features:

- Wayland
- Mesa
- wlroots
- XWayland support
- Desktop environment/window manager


## Phase 9 - Installer

Features:

- Partitioning
- Filesystem creation
- Package installation
- Bootloader setup


## Phase 10 - Binary Infrastructure

Features:

- Build servers
- CI builds
- Package signing
- Repository mirrors

---

## v0.5 — Remote repositories & multi-repo infrastructure

v0.5 turned meowOS into a complete distribution pipeline:

- **Remote Binary Repository Service** — `meow-server` hosts a static repository
  over HTTP; `HttpRepositoryBackend` consumes it with the same trust chain as
  filesystem.
- **Multi-repository config** (`[[repositories]]`), `RepositoryManager`, merged
  view with priority-then-version selection.
- **Dual-backend parity** tests + memory-backed unit tests (disk/network-free).
- **Backend abstraction** (`IRepositoryBackend`) — filesystem, HTTP, in-memory.

## v0.6 — Repository availability, mirrors & diagnostics

- **Repository health state** (`RepositoryStatus` enum: available, network error,
  expired, invalid signature, invalid metadata).
- **Mirror groups** (`mirrors = [...]` sharing `repository_id`, signature, cache).
- **Transport-only failover** (never on trust failures).
- **Parallel metadata refresh** (bounded worker pool).
- **Package history** (append-only audit log, install reasons, `meow why`).
- **Optional dependencies** (metadata-first, `--with-optional` / `--optional`).
- **Package groups** (config-level expansion aliases, atomic install).
- **Doctor diagnostics** (system + security checks).
- **Install locking** (`flock`-based, cross-process).
- **In-process hashing** (no shell execution for sha256).
- **Transaction rollback improvements** (reverse-order file deletion, empty
  directory cleanup).

## v0.7 — Signed package index & SAT resolver

v0.7 closed the per-package trust boundary and added a full SAT-based resolver.

### Signed package index

- `packages.toml` + `packages.toml.sig` authenticates every package manifest
  and artifact hash with the same Ed25519 trusted key.
- `require_package_index` config / env for strict mode.
- Index generated automatically on `meow repo sign`.
- Backwards-compatible (absent index → warn/continue; default `false`).
- Closes `docs/security-audit-v0.5.md` §7 trust-boundary gap.

### SAT resolver

- Dual resolver architecture: `SatResolver` (DPLL over CNF) and
  `LegacyResolver` (DFS-based, compatibility).
- Full version constraint support (`=`, `>=`, `<=`, `>`, `<`).
- Virtual provider resolution natively in SAT (deterministic selection).
- UNSAT diagnostics: `PackageConflict`, `MissingProvider`,
  `VersionConflict`, `Cycle`.
- Correctness parity: 40 synthetic fixtures + full integration suite
  passes under both backends.
- Benchmark suite (`meow-bench`) with seeded, reproducible fixtures.
- RC validation: deterministic large-repo parity check
  (`test/rc/generate_realistic_repo.py` + `compare_resolvers.py`).
- **0 unexpected regressions** in RC comparison.
- `Auto` maps to `Sat` as of v0.7.0; `Legacy` remains selectable via
  `MEOW_RESOLVER=legacy`.

### Security hardening

- Signed package index (closes HIGH #1).
- Install locking (cross-process `flock`).
- In-process checksum verification.
- Transaction rollback hardening.
- Security diagnostics (`doctor --security`).

---

## Release cadence

A lightweight versioning policy so contributors know what kind of change
belongs in each series. The integration suite (`./test/integration.sh`) is a
**release gate** for every tag: a series is not tagged unless all tests pass.

| Series   | Scope                                                        |
|----------|-------------------------------------------------------------|
| v0.5.x   | Remote repositories (HTTP backend, end-to-end client)       |
| v0.6.x   | Mirrors, transport, diagnostics, locking, history           |
| v0.7.x   | Signed package index + SAT resolver + RC validation         |
| v0.8.x   | Mirror health, delta updates, binary cache, hook sandboxing |
| v1.0.0   | Stable package/repository formats and CLI compatibility     |

Rules:

- A `v0.4.x` (and any `.x`) release never introduces format or CLI-breaking
  changes; it refines what already shipped.
- Each new capability series starts from the previous stable tag and is
  merged only when the integration suite is green.
- Format/CLI compatibility guarantees begin at `v1.0.0`; before that,
  breaking changes are acceptable within a major-version-less series as long
  as they are documented in the changelog.
