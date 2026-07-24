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
file m4 bison flex perl
    │
    ▼
base-devel (meta)
    │
    ▼
linux (kernel)
    │
    ▼
initramfs
    │
    ▼
bootloader
    │
    ▼
rootfs image → ISO
```

## Current state

Toolchain (+ downstream userspace): **done** (25 packages built).

Archive tools: `tar`, `gzip`, `xz`, `zstd`.

Build tools: `bison`, `m4`, `zlib`, `patchelf`.

Base packages: `file`, `flex`, `perl`, `neofetch`.

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
