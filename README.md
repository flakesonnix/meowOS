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

## Current Status — Gate 2 (System Validation)

### Gate 2 — Bootstrap Userspace

```
│
├── ✅ Bootstrap toolchain    (binutils → glibc → gcc-stage1 → gcc-stage2 → gcc)
├── ✅ Core userspace         (bash, coreutils, make, pkgconf, grep, sed, gawk, ...)
├── ✅ GNU build stack        (autoconf, automake, libtool, m4, bison, flex, perl)
├── ⏳ base group             (defined in meow.toml; install pending)
├── ⏳ Fresh RootFS install   (bootstrap + group install validation)
├── ⏳ Regression Suite       (verify all tools; meow owns; toolchain smoke tests)
└── ⏳ Exit Checklist         (documentation, clean rebuild, no manual fixes)
```

### Available Packages

| Package | Status |
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
| flex | ✅ |
| perl | ✅ |
| autoconf | ✅ |
| automake | ✅ |
| libtool | ✅ |
| patchelf | ✅ |
| neofetch | ✅ |

## Roadmap

1. 🚧 **Gate 2** — Bootstrap userspace (complete → verify → close)
2. 🔜 **Gate 3** — Self-hosting (meow OS builds meow OS)
3. 🔜 **Gate 4** — Kernel, firmware, initramfs
4. 🔜 **Gate 5** — Image builder / ISO
5. 🔜 **Gate 6** — First self-hosted release

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
