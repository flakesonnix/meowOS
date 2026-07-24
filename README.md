# meowOS

Independent Linux distribution built around the **meow** package manager.

meowOS focuses on a simple package-first architecture: the operating system
itself is built from packages, with reproducible builds, signed repositories,
and transactional upgrades.

## Architecture

- **Package-first design** — everything is a package, including the base system
- **Modern package format** — `.tar.zst` archives with files, metadata, and build information
- **SAT dependency resolver** — DPLL-based CNF solver with legacy DFS fallback
- **Signed trust chain** — Ed25519 signatures for repository metadata and package data
- **Atomic transactions** — install, upgrade, and rollback operations are transactional
- **Parallel downloads, controlled installation** — fast fetching with deterministic package deployment
- **Backend abstraction** — filesystem, HTTP (libcurl), and in-memory backends for testing

## Current Status — v0.7 (July 2026)

### Package Manager

✅ Package manager core  
✅ Signed repositories  
✅ Package index  
✅ SAT dependency resolver  
✅ Transaction system with rollback  
✅ Package ownership tracking  

### Bootstrap Progress

The first self-hosted userspace foundation is being built:

| Component | Status |
|---|---|
| filesystem | ✅ |
| binutils | ✅ |
| glibc | ✅ |
| gcc-stage1 | ✅ |
| gcc-stage2 | ✅ |
| gcc | ✅ |
| bash | ✅ |
| coreutils | ✅ |
| make | ✅ |
| pkgconf | ✅ |
| grep | ✅ |
| sed | ✅ |
| gawk | ✅ |
| findutils | ✅ |
| diffutils | ✅ |
| patch | ✅ |
| tar | ✅ |
| gzip | ✅ |
| xz | ✅ |
| zstd | ✅ |
| bison | ✅ |
| m4 | ✅ |
| zlib | ✅ |

## Roadmap

1. Complete bootstrap userspace
2. Build final `base` package set
3. Generate root filesystem images
4. Add initramfs generation
5. Add bootloader integration
6. Produce installable ISO images

The goal is a complete Linux distribution where the entire system lifecycle —
from bootstrap to upgrades — is handled through the meow package ecosystem.

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
