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
├── ✅ base meta-package      (pkgs/by-name/ba/base; `meow bootstrap <root>` default)
├── ✅ Fresh RootFS install   (`meow bootstrap` → base + 27 deps; idempotent)
├── ⏳ Regression Suite       (verify all tools; meow owns; toolchain smoke tests)
└── ⏳ Exit Checklist         (documentation, clean rebuild, no manual fixes)
```

### Available Packages (36 — Gate 2 complete + extras)

| Package | Version | Status |
|---|---|---|
| filesystem | 1.0.0 | ✅ |
| binutils | 2.46.1 | ✅ |
| glibc | 2.42 | ✅ |
| gcc-stage1 | 15.2.0 | ✅ |
| gcc-stage2 | 15.2.0 | ✅ |
| gcc | 15.2.0 | ✅ |
| bash | 5.3 | ✅ |
| coreutils | 9.6 | ✅ |
| make | 4.4.1 | ✅ |
| pkgconf | 2.4.2 | ✅ |
| grep | 3.11 | ✅ |
| sed | 4.9 | ✅ |
| gawk | 5.3.0 | ✅ |
| findutils | 4.11.0 | ✅ |
| diffutils | 3.12 | ✅ |
| patch | 2.8 | ✅ |
| tar | 1.35 | ✅ |
| gzip | 1.14 | ✅ |
| xz | 5.8.3 | ✅ |
| zstd | 1.5.7 | ✅ |
| bison | 3.8.2 | ✅ |
| m4 | 1.4.21 | ✅ |
| zlib | 1.3.2 | ✅ |
| flex | 2.6.4 | ✅ |
| perl | 5.38.2 | ✅ |
| autoconf | 2.72 | ✅ |
| automake | 1.17 | ✅ |
| libtool | 2.4.7 | ✅ |
| patchelf | 0.18.0 | ✅ |
| neofetch | 7.1.0 | ✅ |
| ncurses | 6.5 | ✅ |
| htop | 3.4.0 | ✅ |
| busybox | 1.37.0 | ✅ |
| strace | 7.0 | ✅ |
| base | 1.0.0 | ✅ meta (27 deps) |
| base-devel | 1.0.0 | ✅ meta (build toolchain) |

> **Idempotent installs:** both resolvers skip packages already installed at the
> same version (`legacy_resolver`, `sat_resolver` check `installedVersion`).
> Re-running `meow install <pkg>` or `meow bootstrap` is a no-op for satisfied deps.
>
> **ISO:** `scripts/mkiso.sh` builds a bootable ISO from a `meow bootstrap` rootfs
> + busybox initramfs (`scripts/init.sh`) + GRUB (`nix develop` provides
> `grub2`, `xorriso`, `squashfsTools`, `cpio`).

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
| `docs/package-groups.md` | Groups vs meta-packages (base is now a meta-package) |
| `docs/architecture.md` | Install/download flow (progress UI, idempotent resolve) |

Full index at `docs/index.md`.

## License

GPLv3
