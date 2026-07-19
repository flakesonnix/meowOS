# meowOS v0.1 — Package Manager

A transactional Linux package manager with signed repositories, dependency resolution, and verify/repair capabilities.

## Features

- Signed repository metadata (Ed25519)
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

## Architecture

```
Repository (index.toml + packages/)
   │
   ▼
Signed index verification (Ed25519)
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
Archive extraction (libarchive, .tar.xz)
   │
   ▼
SQLite database (packages + files metadata)
   │
   ▼
Verify / Repair / Sync / Update
```

## Commands

| Command | Description |
|---------|-------------|
| `meow list` | List available packages |
| `meow search <q>` | Search packages |
| `meow info <pkg>` | Package details |
| `meow install <pkg>` | Install package |
| `meow install --locked <pkg>` | Install from lockfile |
| `meow remove <pkg>` | Remove package |
| `meow upgrade <pkg>` | Upgrade package |
| `meow installed` | List installed packages |
| `meow verify` | Check all installed files |
| `meow repair [pkg]` | Restore missing/modified files |
| `meow sync` | Check for updates |
| `meow update [--dry-run]` | Upgrade all packages |

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

- `index.toml` — metadata index (package names + versions)
- `index.toml.sig` — Ed25519 signature of index
- `public.pem` — Public key for verification
- `packages/<name>/versions/<version>.toml` — Per-version artifact metadata (url, sha256, filename)
- `packages/<name>/package.toml` — Package metadata

## Database

Location: `~/.local/share/meow/database.db` (SQLite)

Tables:
- `packages` — name, version, architecture, install_time
- `files` — path, sha256, size (per installed package)

## Lockfile

Generated after install as `./meow.lock`. TOML format with version, repository_hash, and per-package artifact pins. Enables reproducible `--locked` installs.

## License

GPLv3
