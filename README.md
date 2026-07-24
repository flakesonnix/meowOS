# meowOS — Independent Linux Distribution

meowOS is an independent Linux distribution built around the **meow** package
manager. It focuses on reproducibility, simplicity, signed packages, and a
package-first architecture — everything in the system is a package.

## Features

- **Signed binary packages** — Ed25519 signatures at repo and index level,
  end-to-end artifact verification
- **SAT dependency resolver** — full version-constraint enforcement, UNSAT
  diagnostics, deterministic provider selection
- **Atomic transactions** — install/remove either completes fully or rolls back
- **Self-hosting bootstrap** — cross-compiled from scratch: binutils → glibc
  → gcc → base userspace
- **Reproducible builds** — deterministic archives, `SOURCE_DATE_EPOCH`, sorted
  entries, normalized permissions
- **Signed repository index** — `packages.toml.sig` authenticates every manifest
  and artifact hash
- **Parallel downloads** — bounded worker pool, retries, HTTP resume, ETag caching
- **Mirror groups** — transport-only failover, trust failures never papered over
- **Package history** — append-only audit log, install reasons, `meow why`
- **Doctor diagnostics** — system health + security checks

## Current status

| Component | Status |
|-----------|--------|
| Package manager | ✅ |
| Signed repository | ✅ |
| SAT resolver | ✅ |
| Bootstrap toolchain | ✅ (binutils → glibc → gcc) |
| Base userspace | 🚧 (12 pkgs built, more in progress) |
| Initramfs | ⏳ |
| Bootloader | ⏳ |
| Rootfs image / ISO | ⏳ |

## Installation

meowOS is under active development. Pre-built releases are not yet available.

## Bootstrap

Build a root filesystem from source (requires a Linux host with Nix):

```bash
git clone https://github.com/flakesonnix/meowOS
cd meowOS
nix develop
cmake -B build && cmake --build build
./build/meow-build pkgs/by-name/<shard>/<pkg>/package.toml
```

See `docs/bootstrap.md` for the full bootstrap chain and current progress.

## Bootstrap packages

| Package | Version | Stage |
|---------|---------|-------|
| binutils | 2.46.1 | toolchain |
| glibc | 2.42 | toolchain |
| gcc-stage1 | 15.2.0 | toolchain (C only) |
| gcc-stage2 | 15.2.0 | toolchain |
| gcc | 15.2.0 | toolchain (final) |
| bash | 5.3 | base |
| coreutils | 9.6 | base |
| make | 4.4.1 | base |
| pkgconf | 2.4.2 | base |
| grep | 3.11 | base |
| sed | 4.9 | base |
| gawk | 5.3.0 | base |

Next: `findutils`, `diffutils`, `patch`, `tar`, `gzip`, `xz`, `zstd`.

See `docs/packaging.md` for packaging conventions.

## Documentation

| Document | Audience |
|----------|----------|
| `docs/bootstrap.md` | Bootstrap chain overview |
| `docs/packaging.md` | Packaging guide and conventions |
| `docs/ROADMAP.md` | Full project roadmap |
| `DEVELOPING.md` | Build, test, and contribution guide |
| `docs/repositories.md` | Repository layout and hosting |
| `docs/security.md` | Security model and signing |

Full index at `docs/index.md`.

## License

GPLv3
