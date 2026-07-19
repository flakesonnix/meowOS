# meowOS v0.4.0 — Package Manager

A transactional Linux package manager with signed repositories, dependency resolution, and verify/repair capabilities.

## Features

- Sharded by-name repository layout (no global index)
- Signed repositories (Ed25519)
- Trusted local key store (`~/.config/meow/keys/`)
- Repository metadata expiry (`generated` / `expires`)
- Stable `repository_id` for cache and trust anchoring
- Verified metadata cache (`~/.cache/meow/repos/`)
- `.tar.zst` package archives with metadata + scripts
- Reproducible builds (deterministic archives, `metadata/build.json`)
- Restricted hook runner (isolated cwd, minimal env, timeout, logged output)
- Pre/post-install scripts
- Download abstraction with timeout/TLS options
- Parallel package downloads (bounded worker pool; serial install)
- Package download + SHA256 verification
- Dependency resolution with cycle detection
- Atomic transactions with rollback
- Lockfile support for reproducible installs
- SQLite package database
- Install / Remove / Upgrade
- Verify (detect missing/modified files)
- Repair (restore individual files)
- Sync (check for upstream updates)
- Update (bulk upgrade)
- Doctor (system diagnostics: config, db, trust, cache, integrity, disk)
- `doctor --security` (keys, trust chain, cache, lockfile, hook policy)
- `meow-server`: minimal static HTTP server to host a signed repository
  (`docs/repository-server.md`)

## Architecture

```
Repository (index.toml + packages/)
   │
   ▼
Repository signature verification (Ed25519)
   │
   ▼
Dependency resolver (topological, cycle detection)
   │
   ▼
Downloader (file:// / http(s):// via curl)
   │
   ▼
SHA256 checksum verification
   │
   ▼
Atomic transaction (extract → db commit → rollback on failure)
   │
   ▼
Archive extraction (libarchive, .tar.zst)
   │
   ▼
SQLite database (packages + files metadata)
   │
   ▼
Verify / Repair / Sync / Update
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
| `meow remove <pkg>` | Remove package |
| `meow upgrade <pkg>` | Upgrade package |
| `meow installed` | List installed packages |
| `meow verify` | Check all installed files |
| `meow repair [pkg]` | Restore missing/modified files |
| `meow sync` | Check for updates |
| `meow update [--dry-run]` | Upgrade all packages |
| `meow clean` | Clear the local repository metadata cache |

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

## Repository

A repository contains:

- `repository.toml` — repository metadata (name, schema version)
- `repository.toml.sig` — Ed25519 signature
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
- `packages` — name, version, architecture, install_time
- `files` — path, sha256, size (per installed package)

## Lockfile

Generated after install as `./meow.lock`. TOML format with version, repository_hash, and per-package artifact pins. Enables reproducible `--locked` installs.

## License

GPLv3
