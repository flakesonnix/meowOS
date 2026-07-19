# Reproducible packages

`meow-build` produces **byte-for-byte identical** archives from identical
source + metadata + build environment. This lets the SHA256 in repository
metadata act as a stable *build identity* — useful for signatures,
lockfiles, and supply-chain verification.

```
same source
+ same metadata
+ same build environment
        │
        ▼
same .pkg.tar.zst
        │
        ▼
same sha256
```

## What is normalized

| Aspect        | Rule                                                        |
|---------------|-------------------------------------------------------------|
| Entry order   | Lexicographical by archive path (never filesystem order)    |
| Ownership     | `uid=0`, `gid=0`, `uname=root`, `gname=root`                |
| Timestamps    | `SOURCE_DATE_EPOCH` → `[build].source_date_epoch` → `0`     |
| Permissions   | dirs `0755`, executables `0755`, other files `0644`         |
| Compression   | zstd level `19`, single thread (no `auto` thread count)     |

The `now()` clock is never used for entry metadata, so the wall-clock time
of the build does not leak into the archive.

## package.toml `[build]` table

```toml
[build]
reproducible = true
source_date_epoch = 1720000000
```

- `reproducible` — currently informational; emitted into `build.json`.
- `source_date_epoch` — pins the timestamp for every entry. If absent, the
  `SOURCE_DATE_EPOCH` environment variable is used; if that is also absent,
  `0` is used. The manifest pin takes precedence over the environment
  variable.

## Embedded build metadata

Every archive contains `metadata/build.json`:

```json
{
  "format_version": 1,
  "builder": "meow-build",
  "reproducible": true,
  "source_date_epoch": 1720000000
}
```

This makes each package self-describing and lets tooling verify the build
was produced deterministically.

## Verifying reproducibility

Build the same source twice; the SHA256 of the two `.pkg.tar.zst` files
must match. Changing the source owner, on-disk `mtime`, or directory
iteration order must **not** change the hash. Changing
`SOURCE_DATE_EPOCH` (when not pinned in the manifest) must change it.
