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
- an invalid signature is rejected as before.

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
