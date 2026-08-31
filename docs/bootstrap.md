# Bootstrap

## Chain

```
Host toolchain (Nix)
    │
    ▼
filesystem (base filesystem layout)
    │
    ▼
binutils (assembler, linker, ELF tools)
    │
    ▼
gcc-stage1 (C compiler, no libc, bootstrap)
    │
    ▼
glibc (GNU C Library)
    │
    ▼
gcc (final compiler with libgcc against new glibc)
    │
    ▼
patchelf (ELF patcher for cross-compiled binaries)
    │
    ▼
bash coreutils make pkgconf grep sed gawk
    │
    ▼
findutils diffutils patch  ✅
    │
    ▼
tar gzip xz zstd
    │
    ▼
m4 bison flex perl autoconf automake libtool
    │
    ▼
ncurses htop busybox strace  ✅  (extra userspace)
    │
    ▼
base (meta: 27 deps)  ←  meow bootstrap <root> default
    │
    ▼
linux (kernel)
    │
    ▼
initramfs (busybox + scripts/init.sh via cpio)
    │
    ▼
bootloader (GRUB via grub-mkrescue)
    │
    ▼
rootfs image → ISO  (scripts/mkiso.sh)
```

## Current state — Gate 2

Bootstrap userspace: **36 packages defined, 36 built in repo** (incl. 2 metas: `base`, `base-devel`).

| Segment | Packages | Status |
|---|---|---|
| 🔧 Toolchain | binutils, glibc, gcc-stage1, gcc-stage2, gcc | ✅ complete |
| 📦 Core userspace | filesystem, bash, coreutils, make, pkgconf, grep, sed, gawk, findutils, diffutils, patch | ✅ complete |
| 🗜️ Archive tools | tar, gzip, xz, zstd | ✅ complete |
| 🔨 Build tools | bison, m4, zlib, flex, patchelf | ✅ complete |
| 🐚 Interpreter | perl | ✅ complete |
| 🏗️ GNU build stack | autoconf, automake, libtool | ✅ complete |
| 🎨 Extra | neofetch, ncurses, htop, busybox, strace | ✅ complete |
| **📦 base meta-package** | `pkgs/by-name/ba/base` → `meow bootstrap <root>` default (27 deps, idempotent) | ✅ complete |
| **🏗️ base-devel meta** | `pkgs/base-devel/src` (build toolchain) | ✅ complete |

- `meow bootstrap <root>` installs the `base` meta-package by default (no `--group` flag; `--force` for non-empty target).
- Both resolvers are now **idempotent**: already-installed same-version packages are skipped via `Database::installedVersion`, so re-running `meow bootstrap` or `meow install base` is a no-op for satisfied deps.
- Next: fresh rootfs rebuild + `scripts/mkiso.sh` (busybox initramfs + GRUB ISO; see `flake.nix` devShell for `grub2`/`xorriso`/`cpio`/`squashfsTools`).

## Bootstrapping rules

- Stage N only depends on stages < N.
- No circular deps across stages.
- A package promoted to `gcc` (final) must have been built as `gcc-stage2` against the target `glibc`.
- Package-local workarounds (flags, patches) preferred over builder changes.

## Background

This is a **cross-compiled** bootstrap from a Linux host (NixOS) into the
target meowOS root at `/tmp/meow-install`. The host provides `gcc`, `binutils`,
`make`, and libraries (GMP, MPFR, MPC, ISL). Target packages are built with
`--host=x86_64-pc-linux-gnu` and `--sysroot=/tmp/meow-install`.

See `docs/packaging.md` for packaging conventions and `ROADMAP.md` for the
full project roadmap.
