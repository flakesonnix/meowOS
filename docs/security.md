# Repository trust chain

MeowOS treats repository metadata as untrusted until proven otherwise.
Acceptance follows a strict chain; each step must pass before the next:

```
repository.toml
        |
        v
Ed25519 signature  (repository.toml.sig)
        |
        v
trusted key store  (~/.config/meow/keys/)
        |
        v
repository_id validation  ([a-zA-Z0-9._-])
        |
        v
expiry validation  (expires >= now)
```

## Signature

Repositories are signed with Ed25519. The detached signature
(`repository.toml.sig`) carries a `key_id`. Verification looks up the
matching trusted public key by `key_id`. An unknown `key_id` fails with
`TrustedKeyNotFound`; a bad signature fails with `InvalidSignature`.

Unsigned repositories are accepted only with a warning and should never be
used for production.

### Requiring signatures

To make unsigned (or empty-`keyId`) repositories a hard error instead of a
warning, enable require-signature mode in the config file:

```toml
[security]
require_repository_signature = true
```

Default is `false` (warn-and-continue) for backwards compatibility. When
enabled:

- a repository with no `repository.toml.sig` is rejected with
  `InvalidSignature`;
- a signature file with an empty `keyId` is rejected with `InvalidSignature`;
- a corrupt or malformed `.sig` (e.g. truncated, non-TOML) is rejected with
  `InvalidSignature` — it fails closed rather than propagating a parser error;
- an invalid or mismatched signature (tampered `repository.toml`, or a bad
  signature over HTTP) is rejected as before.

For CI and tests, `MEOW_REQUIRE_SIGNATURE=1` sets the same policy without a
config file.

> **Scope note.** Require-signature mode authenticates the repository **index**
> (`repository.toml`). Per-package manifests and the artifact `sha256` they
> carry are **not yet individually signed** — see the trust-boundary note in
> `docs/security-audit-v0.5.md` §7. For HTTP repositories, TLS (on by default)
> protects manifest integrity in transit.

## Trusted keys

Trusted public keys live in `~/.config/meow/keys/`. Manage them with:

```
meow keys list
meow keys add <public.pem>
```

## Expiry

`repository.toml` may declare `generated` and `expires` as RFC3339 UTC
timestamps. Once `expires` is in the past, the repository is rejected with
`RepositoryExpired`. Expiry is checked only after the signature is verified,
so an attacker cannot forge an expiry state without a valid signature.

## Identity

`repository_id` is the stable anchor for cache directories, mirror selection,
and future trust policy. It is validated after signature verification.

## Caching

The metadata cache under `~/.cache/meow/repos/<repository_id>/` is refreshed
after a successful verification chain. It is **never** used as a source of
trust — cached files are re-verified on every load.

Package archives are fetched over a libcurl-based transport with TLS
verification, redirect following, timeouts, retries on transient errors,
and a `Content-Length` size guard. Downloads are atomic (written to a
`.part` file, then renamed) so a failed or interrupted transfer never
leaves a corrupt cached archive.

## Install locking

Every mutating operation — `install`, `group install`, `remove`, `upgrade`,
`update`, and `repair` — acquires a single process-level advisory lock
(`meow::lock::InstallLock`) for the whole transaction + database-commit
window. The lock is a `flock(2)` on `<db-dir>/install.lock`, so it is safe
across processes and released automatically by the kernel when the holder
exits (including on crash).

- A second concurrent mutating operation fails cleanly with
  `AlreadyLocked` and a diagnostic — it does **not** block and does **not**
  race on the filesystem or the database.
- All mutating paths share the same lock file and path logic, so they are
  mutually exclusive with one another (an install cannot run while a remove
  is in progress, etc.).
- The lock is not held for read-only operations (`list`, `info`, `search`,
  `sync` metadata refresh) — only for operations that write the install root
  or the database.

## Transaction rollback

A mutating operation either commits fully or rolls back:

- **Install / upgrade:** files are extracted to the install root, then the
  database records + history are written in `commitTransaction`. On any
  failure, `rollbackTransaction` deletes the extracted files in reverse order
  and best-effort removes the now-empty parent directories they left, so a
  failed operation does not leave dangling empty directories in the install
  root.
- **Remove:** the database record is removed only after the files are
  deleted; a failure before commit leaves the record intact (fail-safe).
- Downloads are atomic (`.part` → rename), so an interrupted transfer never
  leaves a corrupt cached archive.

### Remaining limitations (known, not blocked)

- Rollback deletes recorded files and empty directories but is **file-delete
  only**: hook side effects (run inside the per-package loop, outside the DB
  commit) are **not** rolled back, and non-empty directories whose contents
  predate the transaction are left in place.
- The lock serializes `meow` processes against each other; it does not guard
  against external tools editing the install root or database directly.
- Package manifests and artifact `sha256` are still authenticated only by the
  repository signature, not a per-package signed index (see
  `docs/package-signing-design.md`, HIGH #1).

