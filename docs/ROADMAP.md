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

## v0.5 Plan (prioritized)

v0.4.0 shipped the core client (doctor, reproducible builds, restricted hooks,
security diagnostics). v0.5 turns meowOS from a package format + client into a
complete distribution pipeline by adding hosting and stronger isolation.

Priority order:

1. **Remote Binary Repository Service** — `meow-server` (done in v0.4.0
   hardening) hosts a static repository over HTTP, and the client now consumes
   it end-to-end via `HttpRepositoryBackend` (added in the v0.5 line):
   `meow` can `list`/`info`/`install` over `http(s)://` with the same trust
   chain as the filesystem backend. The repository abstraction
   (`IRepositoryBackend`) is in place. **Done**: backend abstraction
   (`FilesystemRepositoryBackend` + `HttpRepositoryBackend` + in-memory test
   backend), multi-repository config + `RepositoryManager`, dual-backend parity
   tests, and disk/network-free unit tests. See `docs/repository-selection.md`
   for the next phase.
2. **Repository availability & selection (v0.5.1)** — split out from raw mirror
   work so the behavior is predictable before it is parallel:
   - repository health state (`RepositoryStatus`: available / unavailable /
     network error / expired / invalid signature / invalid metadata);
   - documented priority-then-version selection rule;
   - mirror groups (`mirrors = [...]` sharing one `repository_id`, signature,
     metadata, and cache);
   - failover on transport errors only (never on trust failures);
   - parallel metadata refresh reusing the download-worker pool.
   Design contract: `docs/repository-selection.md`.
3. **Hook sandboxing (real isolation)** — extend the controlled runner with
   Linux namespace isolation (mount namespace, read-only root, seccomp),
   making the hook policy a true security boundary.
4. **SAT solver / better dependency resolution** — conflict reporting for
   constraints like `A requires B >=2` vs `C requires B <2`. Deferred until
   remote repos exist and real larger dependency graphs are available.
5. **Delta packages** — transfer only changed files between versions.
6. **Build farm** — distributed `meow-build` over the repository service.

Note: item 1's server half shipped as `meow-server` in v0.4.0; the client
HTTP wiring was the first v0.5 feature and is now complete.

---

## Release cadence

A lightweight versioning policy so contributors know what kind of change
belongs in each series. The integration suite (`./test/integration.sh`) is a
**release gate** for every tag: a series is not tagged unless all tests pass.

| Series   | Scope                                                        |
|----------|-------------------------------------------------------------|
| v0.4.x   | Bug fixes, documentation, CI, and hardening only            |
| v0.5.x   | Remote repositories (HTTP backend, end-to-end client)       |
| v0.6.x   | Mirrors + transport improvements                            |
| v0.7.x   | SAT solver / advanced dependency resolution                 |
| v0.8.x   | Package signing & reproducible-build ecosystem improvements |
| v1.0.0   | Stable package/repository formats and CLI compatibility     |

Rules:

- A `v0.4.x` (and any `.x`) release never introduces format or CLI-breaking
  changes; it refines what already shipped.
- Each new capability series starts from the previous stable tag and is
  merged only when the integration suite is green.
- Format/CLI compatibility guarantees begin at `v1.0.0`; before that,
  breaking changes are acceptable within a major-version-less series as long
  as they are documented in the changelog.
