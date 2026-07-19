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

## Invalidation

The cache is refreshed atomically (write `.part`, then rename) whenever a
repository is opened and passes verification. To force a clean state:

```
meow clean
```

This removes `~/.cache/meow/repos/`. The next repository open repopulates it.
