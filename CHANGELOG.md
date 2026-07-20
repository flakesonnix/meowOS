# Changelog

## [Unreleased]

### Security

- Secure archive extraction (audit HIGH). Package extraction now applies
  libarchive `ARCHIVE_EXTRACT_SECURE_NODOTDOT | SECURE_SYMLINKS | UNLINK`
  together with explicit entry checks that reject absolute paths, `..`
  traversal, destination escapes, and symlinks whose target escapes the
  extracted tree. The unused, unscoped `extractAll`/`extractFile` helpers
  (which allowed raw traversal) were removed. Regression tests:
  `test/unit/archive_security_test.cpp` (`../` escape, absolute path,
  escaping/absolute symlink, valid package).
- Require-repository-signature mode (audit HIGH/MED). New config option
  `[security] require_repository_signature` (default `false`, preserving
  existing warn-and-continue behavior). When enabled, a repository with no
  `.sig`, an empty `keyId`, or an invalid signature is a hard `InvalidSignature`
  error. Enforced via a process-wide `SecurityPolicy` set from config; also
  togglable with `MEOW_REQUIRE_SIGNATURE=1` for CI/tests. Regression tests:
  `test/unit/security_policy_test.cpp`. See `docs/security.md`.
- Documented the remaining package-metadata trust boundary (per-package
  manifests and artifact hashes are not yet individually signed; requires a
  repository-format change) in `docs/security-audit-v0.5.md` §7.
- Closed remaining extraction and signature gaps (audit follow-up):
  - `loadSignature` now converts a corrupt/malformed `.sig` into the
    `InvalidSignature` error path instead of propagating a raw parser
    exception, preserving fail-closed behavior (audit item 2 — corrupt `.sig`
    bypass).
  - Archive extraction additionally rejects hardlink target escapes, device
    nodes (char/block), FIFOs, and setuid/setgid permission bits; valid
    *internal* symlinks (target inside the package) are still accepted.
  - Expanded regression tests: `test/unit/archive_security_test.cpp`
    (hardlink escape, char/block device, FIFO, setuid/setgid, valid internal
    symlink) and `test/unit/signature_policy_test.cpp` (corrupt `.sig`, empty
    `keyId`, tampered `repository.toml`, unsigned + invalid-signature HTTP
    backends in strict mode).

### Added

- Optional dependencies (phase 1 — metadata only): package manifests and
  repository metadata now support `[[optional_depends]]` with `package` and
  `description`; `meow info` prints an "Optional dependencies" section. No
  resolver or install changes yet — optional deps are metadata, not behavior.
- `docs/optional-dependencies.md`: specification for optional-dependency
  behavior. Fixes the rule that a user-requested optional package is recorded
  as `Explicit` (no new install reason), so future `autoremove` preserves it.
- Optional dependencies (phase 2 — install): `meow install <pkg> --with-optional`
  and `meow install <pkg> --optional <name>` (repeatable) promote selected
  optional packages to additional requested roots before dependency
  resolution; the resolver stays unaware of "optionalness". User-chosen
  optionals are recorded as `Explicit`. An `--optional` name not declared by the
  package is rejected before resolution.
- Package history: every install/remove records an append-only `package_history`
  row with timestamp, action, version, install reason, and a per-transaction
  UUID; the `packages` table now stores a current `install_reason` (Explicit >
  GroupMember > Dependency, upgrade-only). New commands `meow history
  [package]`, `meow why <package>`, and `meow explicitly-installed` expose this
  (`docs/package-history.md`). Existing v1 databases auto-migrate on open.
- `meow-server`: minimal dependency-free static HTTP server that hosts a
  signed repository produced by `meow-repo` (`docs/repository-server.md`).
  Serves `repository.toml`, `repository.toml.sig`, `by-name/...` manifests, and
  `packages/...` artifacts with correct content types, byte-range requests
  (`206 Partial Content`), and ETag/304 caching.
- HTTP repository backend (`HttpRepositoryBackend`): `meow` can now install,
  list, and resolve packages over `http(s)://` repositories. The backend reuses
  the shared `download::` layer (retries, timeouts, ETag/304, partial
  downloads, checksum verification) and follows the **same trust chain** as the
  filesystem backend (signature fetch → trusted-key lookup → verify → expiry
  check). Relative artifact URLs (`packages/<file>`) are resolved against the
  server base URL, keeping repositories portable.
- `--repository <url>` global flag on `meow` to target a specific repository
  (filesystem path, `file://`, or `http(s)://`) without editing config.
- Multiple repositories: config now supports `[[repositories]]` entries with
  `id`, `url`, and `priority`. A new `RepositoryManager` loads all configured
  sources (filesystem and/or HTTP) and presents a merged package space to the
  resolver/installer. Resolution applies **repository priority, then version**:
  for a package present in several repos, the highest-priority repo wins; ties
  break on highest version. Loading is tolerant — one unreachable/expired repo
  is skipped and reported, not fatal. A `--config <path>` flag selects a TOML
  config file; the legacy single `repository = "url"` form is still accepted.
  The cache continues to be keyed by the cryptographic `repository_id`, not the
  config name.
- Doctor is now backend-agnostic: `meow doctor` and `meow doctor --security`
  report **every configured repository** individually (trust, identity,
  expiry, metadata cache) via the `RepositoryManager`, instead of inspecting a
  single fixed repository. A repository that fails to load is surfaced as a
  check (and makes the diagnosis unhealthy) rather than aborting. `sync` and
  `update` already operate on the merged repository view, so they are
   transport-agnostic as well.
- In-memory repository backend (`MemoryRepositoryBackend`) for fast,
  disk/network-free unit tests. It carries a fully built in-memory `Repository`
  plus preloaded package artifacts, so the backend, resolver, and dependency
  closure can be exercised without touching the filesystem or network. The
  `memory://` scheme is reserved for this backend (constructed directly in
  tests; `createBackend` rejects it for production use). A `meow-unit-backend`
  target builds `test/unit/backend_test.cpp`, covering metadata load,
  single-package load (incl. `PackageNotFound`), artifact fetch (present and
  `FileNotFound`), dependency closure, and conflicting-package metadata.
- Integration test section **32. Repository priority selection** locks the
  documented selection contract: higher priority wins over a newer version,
  equal priority breaks on highest version, lower-priority repos fill packages
  missing from higher-priority ones, an unavailable high-priority repo does not
  hide a healthy fallback (and stays visible in the health table), and distinct
  `repository_id`s keep separate caches. `docs/repository-selection.md` gained a
  **Selection algorithm** section stating the procedure as a fixed contract.
- **Parallel metadata refresh**: `RepositoryManager` now refreshes every
  configured source concurrently via `refreshRepositories()`
  (`meow/src/repository/refresh.cpp`), a bounded worker pool reusing the
  download-worker philosophy (default `min(hardware_concurrency, 8)`, overridable
  via `downloadWorkers`). The pool is **not** fail-fast: a broken source is
  recorded and the others keep going. Results return in **input order**, so the
  merged view (priority-then-version) is deterministic regardless of which source
  finished first -- refresh timing can never influence package selection. Each
  source is still verified independently through the failover policy, and cache
  writes stay atomic (temp + rename) keyed by `repository_id` with no new locking
  dependency. Integration test section **35. Parallel repository refresh** covers
  parallel-vs-serial speedup, broken-repo isolation, failover-under-parallel,
  per-`repository_id` cache isolation, and deterministic selection across repeated
  refreshes.
- **Package groups** (`meow group list` / `meow group install`): a group is a
  named local expansion alias over package names, declared as `[[groups]]` in
  `meow.toml` (`config::PackageGroup`). `meow group install <name>` expands to
  the members and installs them through the *same* staging path as
  `meow install` (`resolveAndStage` → parallel download → `installPackages`),
  so the two commands share identical failure semantics. The install is
  **atomic**: the whole expansion is one dependency closure committed in a
  single transaction, so a group either installs completely or changes nothing
  (it is not a loop of per-package installs). Groups are expansion aliases, not
  package identities -- the database records the individual packages, never a
  synthetic group entity. The config loader rejects malformed groups strictly:
  empty name, duplicate name, empty member list, or a name colliding with a
  reserved CLI command. `docs/package-groups.md` documents the contract.
  Integration test section **package groups** covers config parse, list,
  install, dependency closure, atomicity, missing-group and duplicate-group
  rejection (plus a reserved-name rejection check).
- **Mirror groups** (data model): a configured source is now one *repository
  identity* served from one or more *mirrors*. Config accepts
  `mirrors = ["url", ...]`; the legacy single `url = "..."` form is migrated
  internally to a one-element mirror list. All mirrors of a source must yield the
  same `repository_id`, signature, and metadata, and they share a single cache
  keyed by `repository_id` (never by mirror path). Priority is per source (group),
  not per mirror. Transport-level load failures fall through to the next mirror;
  trust failures (bad signature, expired, invalid id) stop the group rather than
   being papered over by another mirror. Integration test section
   **33. Repository mirror groups** covers single-mirror, shared `repository_id`,
   cache keying, priority preservation, and legacy `url` migration.
- **Mirror failover policy**: `RepositoryManager` now fails over across a
  source's mirrors using `loadRepositoryWithFailover()` /
  `isFailoverAllowed()` (`meow/src/repository/failover.cpp`). Only *transport*
  failures are retried on the next mirror: timeout, DNS/connection refused/reset,
  and HTTP `5xx` (new `DownloadHttp5xx` error code). *Trust* failures are NOT
  failed over -- bad signature, expired metadata, invalid `repository_id` /
  metadata, checksum mismatch, and HTTP `4xx` (incl. `404`) abort the chain
  immediately so a bad mirror is never papered over by another copy of the same
  untrusted data. Every attempted mirror is recorded in
  `RepositoryState::attempts` and surfaced in the `meow sync` health table; a
  failed mirror is never silently dropped. Config is unchanged (`mirrors = [...]`).
  Integration test section **34. Repository mirror failover** covers offline
  fallback, timeout fallback, bad-signature abort (no fallback), HTTP 404 abort
  (no fallback), and both-mirrors-down -> `NetworkError` with preserved attempts.

## [0.4.0] - 2026-07-19

### Added

- Reproducible package generation: `meow-build` emits byte-identical
  `.pkg.tar.zst` archives for identical source + metadata + environment
  (sorted entries, root ownership, `SOURCE_DATE_EPOCH` timestamps,
  normalized permissions, fixed zstd level/threads)
- `[build]` table in package.toml (`reproducible`, `source_date_epoch`)
  and self-describing `metadata/build.json` embedded in every archive
- Restricted hook runner (`meow/hooks`): package scripts run in an isolated
  staging cwd with a minimal environment, captured logging, and a timeout
  (SIGTERM → SIGKILL). New error codes `HookFailed`, `HookTimeout`,
  `HookDenied`. A failed hook rolls back the install transaction.
- `[hooks]` config: `timeout`, `network` (advisory), `inheritEnvironment`
- `meow doctor` system diagnostics command (config, database schema,
  repository trust/identity/expiry, cache, lockfile consistency, installed
  file integrity summary, disk space, permissions) with `--json` output
- `meow doctor --security` focused security report (trusted keys, trust
  chain signature→key→identity→expiry, cache partials, lockfile integrity,
  hook policy). Read-only; never mutates state.
- Parallel package downloads: the dependency closure is resolved from
  repository metadata (no downloads), then all artifacts are fetched
  concurrently via a bounded worker pool (`meow/download/queue.hpp`);
  installation remains strictly serial
- Dependency closure resolution from repository metadata only
  (`resolveDependencyNames`), with cycle detection
- `download_workers` config option (0 = default `min(hardware_concurrency, 8)`)

### Changed

- Download transport rewritten on libcurl (no shell execution); captures
  HTTP status codes, headers, and curl errors
- Download robustness: atomic writes (`.part` + rename), HTTP resume,
  retries, timeouts, and ETag `If-None-Match` passthrough; `Content-Length`
  size guard (`maxBytes`) to reject oversized downloads early;
  `304 Not Modified` reuses the cached file
- New download errors: `DownloadTimeout`, `DownloadHttpError`,
  `DownloadInterrupted`, `InvalidDownload`; `DownloadResult` (path, size,
  etag, last-modified, 304 flag) and `DownloadOptions` (retries, timeout,
  TLS, etag, maxBytes)
- `log::setLevel` / `log::getLevel` to control emitted log verbosity
- `database::checkSchema` to verify the on-disk schema tables
- Self-contained integration suite: builds its own package fixtures
  (no dependency on pre-existing `/tmp` artifacts)

### Security

- Trusted local key store (`~/.config/meow/keys/`) for Ed25519 verification
- Repository metadata signature verification anchored to trusted keys
- Stable `repository_id` for cache and trust anchoring
- Repository metadata expiry (`generated` / `expires`) enforcement
- Signed, verified repository metadata cache (`~/.cache/meow/repos/`)
- Hook execution isolated (staging cwd, minimal env, timeout); network
  policy is advisory until OS-level sandboxing lands

## [0.3.0] - 2026-07-19

### Added

- Dependency constraints with version operators:
  - `>=`
  - `<=`
  - `>`
  - `<`
  - `=`
- Dependency resolver support for:
  - provides
  - conflicts
  - version matching
- File ownership queries:
  - `meow owns`
- Reverse dependency tracking:
  - `meow required-by`
- Dependency protection during removal
- Package builder:
  - `meow-build`
  - creates `.pkg.tar.zst` packages
- Repository tooling:
  - add/remove/sync/sign
  - sharded by-name repository layout
- Database tracking:
  - package dependencies
  - package provides
- Integration test suite:
  - 22 end-to-end scenarios
- `--db-path` for isolated testing

### Fixed

- Crypto signing buffer overflow caused by EVP_EncodeBlock usage
- CLI flag comparison bug (`argv[i] == flag`)
- Resolver duplicate dependency traversal causing false cycle detection

### Changed

- Dependencies now store constraints instead of only package names
- Repository samples expanded:
  - `libfoo`
  - `app`
  - provides/conflicts examples

## [0.2.0] - 2026-07-19

### Added

- By-name sharded repository layout (`by-name/<shard>/<package>/`)
- Repository mirror support in `repository.toml`
- `openRepository` protocol with mirror fallback
- `meow-repo` architecture (scaffolded)

### Changed

- Repository index format changed from flat to sharded by-name layout
- Repository loading now uses `openRepository` reader abstraction
- Split monolithic repository module into reader/writer roles

### Fixed

- Repository index resolution with sharded path lookups
- Repository metadata now uses `[metadata]` section consistently

## [0.1.0] - 2026-07-19

### Added

- Package format v1 (`.tar.zst` with `files/`, `scripts/`, `metadata/` prefixes)
- Signed repositories (ed25519 via OpenSSL)
- Transaction system with commit/rollback
- SQLite database for installed packages
- Dependency solver with DAG resolution and cycle detection
- Package lifecycle: install, remove, upgrade, update, verify, repair
- Lockfile support for reproducible installations
- CLI commands:
  - `meow install`, `meow remove`, `meow upgrade`
  - `meow update`, `meow verify`, `meow repair`
  - `meow list`, `meow info`, `meow search`
- Repository package queries: list, search, info, version resolution
- CI: GitHub Actions with Nix build on push

### Changed

- Error handling migrated to typed `MeowError` with `ErrorCode`
- Archive handling uses RAII `ArchiveHandle`
- Core types in `meow::types` namespace (PackageName, PackageVersion, etc.)

### Fixed

- Archive extraction with sorted file creation (directories before files)
- Dependency resolution now properly detects cycles
