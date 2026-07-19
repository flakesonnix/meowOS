# Metadata cache

MeowOS caches verified repository metadata to speed up repeated operations
and to provide a stable identity-based layout for future HTTP transport.

## Location

```
~/.cache/meow/repos/
└── <repository_id>/
    ├── repository.toml      — cached metadata
    ├── repository.toml.sig  — cached signature
    └── metadata.json        — cache bookkeeping
```

`metadata.json` currently records:

```json
{
  "etag": "",
  "last_checked": "2026-07-19T12:00:00Z"
}
```

`etag` is populated from the repository metadata transport (HTTP
`ETag` header via `If-None-Match` / `304 Not Modified`) once the
repository metadata cache is wired to the HTTP transport. For `file://`
repositories it is empty. The artifact download cache (under
`~/.cache/meow/`) uses the same libcurl transport and supports
`If-None-Match` for package archives.

## Security

The cache is strictly a **transport optimization**. Cached `repository.toml`
is re-verified (signature → identity → expiry) on every load. Never disable
verification for cached metadata.

## Parallel refresh

`meow sync` refreshes every configured repository concurrently in a bounded
worker pool (`refreshRepositories()`), reusing the download-worker pool
philosophy. Each source is verified independently through the failover policy.

Cache behavior under concurrent refresh:

- The cache is keyed by the cryptographic `repository_id`, never by mirror URL.
  Two configured sources that resolve to the same `repository_id` share one
  cache directory; two sources with different `repository_id`s always get
  distinct directories and never collide.
- Writes are atomic: a temporary `.part` file is written, then renamed into
  place. This means a cache directory is never observed half-written, even if
  two refreshes target the same `repository_id` simultaneously. No external lock
  file or locking dependency is introduced.
- A refresh that fails trust verification (`InvalidSignature`, `Expired`,
  `InvalidMetadata`) is excluded from the merged view but its attempt history is
  still recorded; a failed source is never silently dropped.

## Invalidation

The cache is refreshed atomically (write `.part`, then rename) whenever a
repository is opened and passes verification. To force a clean state:

```
meow clean
```

This removes `~/.cache/meow/repos/`. The next repository open repopulates it.
