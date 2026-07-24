# Developing meowOS

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

### Building packages

Bootstrap packages are built with `meow-build`:

```bash
./build/meow-build pkgs/by-name/<shard>/<pkg>/package.toml
```

See `docs/bootstrap.md` for the bootstrap chain and `docs/packaging.md`
for packaging conventions.

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
   └── Legacy — DFS-based, cycle detection
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

## Resolver selection

Set `MEOW_RESOLVER=sat` or `MEOW_RESOLVER=legacy` to switch. The default
(`Auto`) maps to `SatResolver`. Legacy is available via `MEOW_RESOLVER=legacy`
or `ResolverEngine::Legacy`.

## Repository layout

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

## Package format

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

Generated after install as `./meow.lock`. TOML format with version,
repository_hash, and per-package artifact pins. Enables reproducible
`--locked` installs.

## Resolver internals

### SAT resolver

- DPLL over CNF
- Full version-constraint enforcement (`=`, `>=`, `<=`, `>`, `<`)
- Virtual provider resolution (deterministic)
- UNSAT diagnostics: `PackageConflict`, `MissingProvider`, `VersionConflict`, `Cycle`

### Legacy resolver

- DFS-based
- Cycle detection
- No version-constraint enforcement

### Parity

40 synthetic fixtures + full integration suite passes under both backends.
Intentional divergences documented.

## Quality assurance

- Full integration test suite (CTest `-L integration`)
- C++ unit tests (CTest `-L unit`, disk/network-free)
- SAT resolver benchmark (`meow-bench`)
- RC validation — deterministic, large-scale resolver parity testing
  (`test/rc/generate_realistic_repo.py` + `compare_resolvers.py`)
