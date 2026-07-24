# Packaging guide

## Directory layout

```
pkgs/by-name/<shard>/<package>/
    ├── package.toml      — metadata + build phases
    └── files/            — static files (optional, overlayed onto DESTDIR)
```

Shard = first 2 lowercase chars of package name.
Single-char names use `<char>_`.

## package.toml fields

| Field | Required | Description |
|-------|----------|-------------|
| `name` | yes | Package name, lowercase, no spaces |
| `version` | yes | Upstream version string |
| `architecture` | yes | Target arch (e.g. `AMD64`) |
| `description` | yes | Short one-line description |
| `license` | yes | SPDX identifier |
| `depends` | yes | List of runtime dependencies |
| `source.url` | yes | Upstream source tarball URL |
| `source.sha256` | yes | Source checksum |
| `phases` | yes | Build phases (see below) |

Recommended but optional:

- `homepage` — project URL
- `maintainer` — name/email

## Build phases

Defined under `[phases]`. Available phase types:

| Phase | Required | Description |
|-------|----------|-------------|
| `configure` | yes | `./configure` or equivalent |
| `build` | yes | `make` or equivalent |
| `install` | yes | `make install DESTDIR="${out}"` |
| `check` | no | Test suite (not run during bootstrap) |

Available variables in phase scripts:

- `${src}` — extracted source directory
- `${build}` — separate build directory
- `${out}` — package destination (`DESTDIR`)
- `${MEOW_JOBS}` — parallel job count

## Cross-compilation convention

Every package targeting meowOS must:

- Set `CC="gcc --sysroot=/tmp/meow-install"`
- Pass `--host=x86_64-pc-linux-gnu` to configure
- Set `CFLAGS="-O2"` (add `-Wno-error=*` only when GCC 15 flags an error)

Example:

```
export CC="gcc --sysroot=$_SYSROOT"
export CFLAGS="-O2"
"${src}/configure" --prefix=/usr --host=x86_64-pc-linux-gnu
```

## ELF patching

All binaries in `/usr/bin/` must have interpreter and rpath set for the
target root. Add this to the install phase:

```
shopt -s nullglob
for _f in "${out}/usr/bin/"*; do
  if file "$_f" 2>/dev/null | grep -q "ELF.*executable"; then
    patchelf --set-interpreter /lib64/ld-linux-x86-64.so.2 \
             --set-rpath /usr/lib:/usr/lib64 "$_f"
  fi
done
```

## Fixes and workarounds

- **Package-local first.** A flag, configure switch, or small patch in the
  `package.toml` is always preferred over changing the builder.
- **Justify non-obvious flags.** Add a brief comment above the flag
  (e.g. `# GCC 15 compat`).
- **No vendored deps.** Download and symlink into the source tree in the
  configure phase (see gcc for the pattern).
- **`remove info/dir`.** Every install phase must `rm -f "${out}/usr/share/info/dir"`.

## Versioning

- Follow upstream version verbatim.
- When patching upstream, append `.meow<N>` (e.g. `5.3.0.meow1`).
- Bootstrap stages share the upstream version but have distinct package names
  (`gcc-stage1`, `gcc-stage2`, `gcc`).

## When to add a new package

Create `pkgs/by-name/<shard>/<pkgname>/package.toml` and build with:

```
./build/meow-build pkgs/by-name/<shard>/<pkgname>/package.toml
```

Commit the `package.toml` and the built artifact to `repo/packages/`.
Maintain shard layout—do not commit to an incorrect shard.
