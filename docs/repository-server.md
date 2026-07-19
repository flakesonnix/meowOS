# Remote Repository Server (`meow-server`)

`meow-server` is a minimal, dependency-free static HTTP server that hosts a
meow repository produced by `meow-repo`. It lets you expose a signed repository
over the network so clients can fetch metadata, manifests, and package
artifacts.

It is intentionally a thin static file server: it performs no signing, no
transformation, and no dynamic indexing. All trust and integrity is enforced
by the client (`meow`) using the repository signature and per-artifact hashes.

## Usage

```text
meow-server serve <repo-dir> [--port <port>]
```

Example:

```text
meow-server serve ./repo --port 8080
# serves http://0.0.0.0:8080
```

## What it serves

Given a repository laid out by `meow-repo`:

```text
repo/
├── repository.toml          # served at /repository.toml
├── repository.toml.sig      # served at /repository.toml.sig
├── by-name/
│   └── he/hello/
│       ├── package.toml
│       └── versions/1.1.0.toml
└── packages/
    └── hello-1.1.0.pkg.tar.zst
```

`meow-server` maps request paths directly onto files under the served root:

| Request                        | Serves                                  |
| ------------------------------ | --------------------------------------- |
| `/` or `/repository.toml`      | `repository.toml` (TOML content type)   |
| `/repository.toml.sig`         | signature (octet-stream)                |
| `/by-name/he/hello/package.toml` | package manifest                      |
| `/packages/hello-1.1.0.pkg.tar.zst` | package artifact (octet-stream)  |

## Behavior

- **GET / HEAD** supported; all other methods return `405`.
- **Range requests** (`Range: bytes=...`) return `206 Partial Content`, so
  large artifacts can be resumed and partially fetched.
- **ETag + 304** are emitted for client-side caching. The ETag is derived from
  file size and modification time.
- **Content types**: `.toml` → `application/toml; charset=utf-8`; `.sig` and
  package archives → `application/octet-stream`.
- **Path traversal** outside the served root is rejected (`404`).
- **`404`** for missing files.

## Security model

`meow-server` is unauthenticated and trusts the filesystem. Security comes
from the client side:

- repository metadata is verified against a trusted Ed25519 key;
- each artifact is checked against the `sha256` recorded in its manifest;
- repository expiry (`generated` / `expires`) is enforced by the client.

Do not place secret material inside a served repository directory.

## Distribution pipeline

```text
meow-build  ->  package artifact
meow-repo   ->  signed repository (repository.toml + by-name + packages)
meow-server ->  serve over HTTP
meow        ->  sync + install + verify
```
