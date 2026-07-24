# meowOS — Package Manager & Linux Distribution

A transactional Linux package manager (v0.7) with signed repositories, SAT-powered
dependency resolution, signed package index, and verify/repair capabilities.
Bootstrap packages for a full meowOS distribution are in active development.

## Features

### Resolver

- **Dual resolver architecture** — SAT-based (`SatResolver`) and DFS-based
  (`LegacyResolver`) backends, selectable via `MEOW_RESOLVER` env / config
- **SAT resolver** — DPLL over CNF with full version-constraint enforcement
  (`=`, `>=`, `<=`, `>`, `<`), virtual provider resolution, UNSAT diagnostics
- **Dual backend** — select via `MEOW_RESOLVER=sat` or `MEOW_RESOLVER=legacy`;
  `Auto` maps to `Sat` (default); `Legacy` available as escape hatch
- **Virtual provider resolution** — SAT resolves virtual dependencies
  natively (no special-casing), with deterministic provider selection
- **UNSAT diagnostics** — structured `PackageConflict` / `MissingProvider` /
  `VersionConflict` / `Cycle` reporting
- **Correctness parity** — 40 synthetic fixtures + full integration suite
  passes under both backends; intentional divergences documented

### Security

- Signed repositories (Ed25519) with trusted key store
- **Signed package index (v0.7)** — `packages.toml` + `packages.toml.sig`
  authenticates every package manifest and artifact hash end-to-end
- `require_package_index` config / env — optional strict mode
- Repository metadata expiry (`generated` / `expires`)
- Stable `repository_id` for cache and trust anchoring
- Verified metadata cache (`~/.cache/meow/repos/`)
- In-process SHA256 verification (no shell execution)
- Install locking (`flock`-based, cross-process)
- Atomic transaction with rollback
- `doctor --security` (keys, trust chain, cache, lockfile, hook policy)

### Packaging

- `.tar.zst` package archives with metadata + scripts
- Sharded by-name repository layout (no global index)
- Reproducible builds (deterministic archives, `metadata/build.json`)
- Optional dependencies (metadata-first, opt-in install)
- Package groups (config-level expansion aliases)

### Transport

- libcurl-based download (no shell execution)
- TLS verification, timeouts, retries on transient failures
- Atomic `.part` + rename writes
- HTTP `ETag` / `304` support for caching
- Parallel package downloads (bounded worker pool; serial install)
- Filesystem + HTTP repository backends
- Mirror groups with transport-only failover
- Parallel metadata refresh

### Operations

- Install / Remove / Upgrade / Sync / Update
- Verify (detect missing/modified files)
- Repair (restore individual files)
- Doctor (system diagnostics: config, db, trust, cache, integrity, disk)
- Lockfile support for reproducible installs (`--locked`)
- SQLite package database with install-reason tracking
- Package history (append-only audit log)
- `meow-server`: minimal static HTTP server for signed repositories
- `meow-repo`: repository builder with automatic index signing
- `meow-build`: reproducible package builder

### Bootstrap packages

Available under `pkgs/by-name/` (built with `meow-build`):

| Package | Version | Notes |
|---------|---------|-------|
| `binutils` | 2.46.1 | Assembler, linker, binary tools |
| `glibc` | 2.42 | GNU C Library |
| `gcc-stage1` | 15.2.0 | Stage1 (C only, bootstrap) |
| `gcc-stage2` | 15.2.0 | Stage2 (with libgcc, against new glibc) |
| `gcc` | 15.2.0 | Final compiler (synthetic promote of stage2) |
| `bash` | 5.3 | GNU Bourne Again SHell |
| `coreutils` | 9.6 | GNU core utilities |
| `make` | 4.4.1 | GNU make |
| `pkgconf` | 2.4.2 | Package compiler/linker metadata toolkit |
| `grep` | 3.11 | GNU grep |
| `sed` | 4.9 | GNU sed (`--disable-acl`) |
| `gawk` | 5.3.0 | GNU awk (`-Wno-error=incompatible-pointer-types`) |

Planned next: `findutils`, `diffutils`, `patch`, `tar`, `gzip`, `xz`, `zstd`.

See `docs/ROADMAP.md` for the full bootstrap plan.

### Quality assurance

- Full integration test suite (CTest `-L integration`)
- C++ unit tests (CTest `-L unit`, disk/network-free)
- SAT resolver benchmark (`meow-bench`)
- **RC validation** — deterministic, large-scale resolver parity testing
  (`test/rc/generate_realistic_repo.py` + `compare_resolvers.py`)

## Architecture

```
Repository (by-name layout)
   │
   ▼
Repository signature verification (Ed25519)
   │
   ▼
Signed package index verification (packages.toml.sig)
   │
   ▼
Dependency resolver
   ├── SAT (selectable) — DPLL over CNF, version constraints, UNSAT diagnostics
   └── Legacy (current default) — DFS-based, cycle detection
   │
   ▼
Downloader (file:// / http(s):// via curl)
   │
   ▼
SHA256 checksum verification (against signed index)
   │
   ▼
Atomic transaction (extract → db commit → rollback on failure)
   │
   ▼
Archive extraction (libarchive, .tar.zst)
   │
   ▼
SQLite database (packages + files metadata + history)
   │
   ▼
Verify / Repair / Sync / Update / Doctor
```

## Distribution pipeline

```text
meow-build    ->  package artifact (.pkg.tar.zst, reproducible)
meow-repo     ->  signed repository (repository.toml + by-name/ + packages/)
meow-server   ->  serve the repository over HTTP
meow          ->  sync + install + verify
```

See `docs/repository-server.md` for hosting a repository with `meow-server`.

## Commands

| Command | Description |
|---------|-------------|
| `meow list` | List available packages |
| `meow search <q>` | Search packages |
| `meow info <pkg>` | Package details |
| `meow install <pkg>` | Install package (runs pre/post install scripts) |
| `meow install --locked <pkg>` | Install from lockfile |
| `meow install --with-optional` | Install with optional dependencies |
| `meow install --optional <name>` | Install specific optional deps |
| `meow remove <pkg>` | Remove package |
| `meow upgrade <pkg>` | Upgrade package |
| `meow installed` | List installed packages |
| `meow verify` | Check all installed files |
| `meow repair [pkg]` | Restore missing/modified files |
| `meow sync` | Check for updates |
| `meow update [--dry-run]` | Upgrade all packages |
| `meow clean` | Clear the local repository metadata cache |
| `meow history [pkg]` | Show install/remove audit log |
| `meow why <pkg>` | Show install reason and reverse deps |
| `meow explicitly-installed` | List explicitly installed packages |
| `meow group list` | List defined package groups |
| `meow group install <group>` | Install a package group atomically |
| `meow doctor` | System diagnostics |
| `meow doctor --security` | Security-focused diagnostics |
| `meow keys list` | List trusted signing keys |
| `meow keys add <key>` | Add a trusted signing key |

## Transport

Downloads use a libcurl-based transport (no shell execution) with TLS
verification, timeouts, retries on transient failures, atomic
`.part`+rename writes, checksum verification, and HTTP `ETag`/`304`
support for caching.

## Build

Requires: CMake 3.20+, C++20 compiler, SQLite3, libarchive, OpenSSL, tomlplusplus.

```bash
cmake -B build
cmake --build build
./build/meowOS list
```

On Nix:

```bash
nix develop
cmake -B build
cmake --build build
```

## Testing

```bash
# full suite (both resolvers)
MEOW_RESOLVER=sat    ctest --output-on-failure
MEOW_RESOLVER=legacy ctest --output-on-failure

# unit tests only
ctest -L unit

# integration tests only
ctest -L integration

# RC validation (deterministic large-repo parity check)
python3 test/rc/generate_realistic_repo.py
MEOW_RESOLVER=legacy python3 test/rc/compare_resolvers.py
MEOW_RESOLVER=sat    python3 test/rc/compare_resolvers.py
```

## Resolver selection

Set `MEOW_RESOLVER=sat` or `MEOW_RESOLVER=legacy` to switch. The default
(`Auto`) currently maps to `LegacyResolver`. SAT is recommended and can be
selected via `MEOW_RESOLVER=sat`; the default flip is planned after v0.7.0.

## Repository

A repository contains:

- `repository.toml` — repository metadata (name, schema version)
- `repository.toml.sig` — Ed25519 signature
- `packages.toml` — signed package index (v0.7+, authenticates every manifest + artifact hash)
- `packages.toml.sig` — Ed25519 signature for the package index
- `public.pem` — Signing public key
- `by-name/<shard>/<package>/package.toml` — Package metadata
- `by-name/<shard>/<package>/versions/<version>.toml` — Per-version artifact metadata (url, sha256, filename)

The shard directory is the first two lowercase characters of the package name.
Single-character names use `<char>_` as the shard. This layout scales to millions
of packages without a single monolithic index file.

## Package Format

Archives use `.tar.zst` (Zstandard compression) with this internal layout:

```
package.toml          — Package metadata (name, version, arch, license, homepage, maintainer, depends, conflicts, provides, replaces)
files/                — Files to install (prefix stripped during extraction)
scripts/              — Pre/post install/remove scripts
metadata/licenses/    — License files
metadata/changelog    — Change history
```

## Database

Location: `~/.local/share/meow/database.db` (SQLite)

Tables:
- `packages` — name, version, architecture, install_time, install_reason
- `files` — path, sha256, size (per installed package)
- `package_history` — append-only audit log (action, timestamp, transaction_id)
- `package_deps` — recorded dependency edges
- `package_provides` — recorded provides edges

## Lockfile

Generated after install as `./meow.lock`. TOML format with version, repository_hash, and per-package artifact pins. Enables reproducible `--locked` installs.

## License

GPLv3
