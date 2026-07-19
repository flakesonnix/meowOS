# Changelog

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
