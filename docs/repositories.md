# Repositories

MeowOS uses signed, sharded repositories. A repository is a directory or
remote URL containing package metadata and a signed `repository.toml`.

## Layout

```
repository.toml          — repository metadata + signature anchor
repository.toml.sig      — Ed25519 detached signature
public.pem               — signing public key (distributed out of band)
by-name/<shard>/<pkg>/   — per-package metadata (see below)
packages/                — optional package archives
```

`by-name` sharding scales to millions of packages without a monolithic index.
The shard is the first two lowercase characters of the package name; a
single-character name uses `<char>_` as the shard.

## repository.toml

```toml
format_version = 1
name = "local"
repository_id = "main"

generated = "2026-07-19T12:00:00Z"
expires = "2026-08-18T12:00:00Z"

[[mirror]]
url = "./repo"
priority = 10
```

| Field           | Required | Meaning                                              |
|-----------------|----------|------------------------------------------------------|
| `format_version`| yes      | Metadata schema version (currently `1`).             |
| `name`          | yes      | Human-readable repository name.                       |
| `repository_id` | yes      | Stable identity used for cache dirs and trust policy. |
| `generated`     | no*      | RFC3339 UTC creation timestamp.                      |
| `expires`       | no*      | RFC3339 UTC expiry; past = `RepositoryExpired`.      |
| `mirror`        | no       | Mirror list (fallback source URLs).                  |

\* Recommended. `expires` enables metadata expiry validation.

`repository_id` must match `[a-zA-Z0-9._-]`. It is validated **after**
signature verification. A missing or invalid `repository_id` fails with
`InvalidRepository`.

## Sync behavior

`meow sync` (and any command that opens a repository) performs:

1. Read `repository.toml`.
2. Verify the Ed25519 signature against a trusted key.
3. Validate `repository_id`.
4. Validate `expires` (if present).
5. Refresh the local metadata cache.

The cache is a **transport optimization only**. Cached metadata is never
trusted without re-running signature verification.

## Multiple repositories

Each repository is keyed by `repository_id`. Mirror selection, cache
directories, and trust policy are anchored to this identity rather than the
configured URL, so URL or mirror changes do not disturb local state.

## Backends (transport abstraction)

`meow` accesses every repository through a `meow::repository::IRepositoryBackend`
interface (`loadRepository()`, `loadPackage()`, `fetchArtifact()`). Concrete
backends currently implemented:

| Backend                  | URL scheme(s)                | Notes                              |
|--------------------------|------------------------------|------------------------------------|
| `FilesystemRepositoryBackend` | `file://`, local path   | Scans `by-name/`, verifies signature on disk |
| `HttpRepositoryBackend`  | `http://`, `https://`        | Reuses the shared `download::` layer; same trust chain as filesystem |

Both backends follow the identical trust chain (signature fetch → trusted-key
lookup → verify → expiry check). Network traffic always goes through the shared
downloader, so retries, timeouts, ETag/304 caching, partial downloads, and
checksum verification are uniform across transports.

Artifact URLs in `versions/<ver>.toml` may be **relative** (`packages/<file>`);
the HTTP backend resolves them against the server base URL, keeping a
repository portable across hosts. Absolute `file://` / `http(s)://` URLs are
used as-is.

A repository served over HTTP is enumerated via an optional `packages.index`
file (newline-separated `name version` pairs) at the repository root, since the
static server performs no directory listing.
