# Repository selection, health, and mirrors

This document defines how `meow` chooses among configured repositories, how it
reports and tolerates repository failures, and how mirrors and parallel refresh
behave. It is the design contract for the distribution-scale repository work
that follows the v0.5 backend milestone (filesystem + HTTP + in-memory backends,
multi-repository config, dual-backend parity, and disk/network-free unit tests).

The guiding principle: **a broken repository must never destroy a whole
operation.** Availability is per-source; selection is deterministic and
documented; trust is never relaxed for availability.

## Terminology

- **Repository** — a signed package source identified by a cryptographic
  `repository_id` (from `repository.toml`). The same `repository_id` served from
  several URLs is the *same* repository reaching via different *mirrors*.
- **Mirror** — one transport endpoint that serves a given repository
  (`file://`, local path, or `http(s)://`). Mirrors of the same repository share
  the `repository_id`, signature, metadata, and cache.
- **Configured source** — one entry under `[[repositories]]` in `meow.toml`. A
  source is one *repository identity* served from one or more *mirrors*
  (transport locations). The legacy single `url = "..."` form is still accepted
  and is internally migrated to a one-element `mirrors = ["..."]` list.
- **Priority** — an integer on a configured source. Higher priority wins when a
  package exists in more than one source.
- **Backend** — the transport implementation behind a URL scheme
  (`FilesystemRepositoryBackend`, `HttpRepositoryBackend`,
  `MemoryRepositoryBackend` for tests).

## Current behavior (baseline)

The following already ships and is the contract for the rules below.

- Config supports `[[repositories]]` with `id`, `priority`, and a `mirrors`
  list of URLs (`file://`, local path, or `http(s)://`). The legacy single
  `url = "..."` form is still accepted and mapped to one source with
  `mirrors = ["..."]`. A source is a *repository identity*; all its mirrors must
  yield the same `repository_id`, signature, and metadata. Priority is per
  source (group), not per mirror.
- `RepositoryManager` loads every configured source through `createBackend`.
  Loading is **tolerant**: a source that throws (network error, missing repo,
  expired, bad signature) is recorded in `failures()` and skipped. The rest of
  the operation continues. If *zero* sources load, the command fails loudly with
  the underlying error text.
- Merged view selection rule: **priority first, version second.** For a package
  present in several sources, the highest-priority source wins; ties in priority
  break on the highest latest version. This is implemented in
  `RepositoryManager::buildMerged()`.
- The resolver/installer operate only on the merged `Repository`, so they are
  transport-agnostic.
- Cache is keyed by the cryptographic `repository_id`, never by the config `id`
  or URL. Two config entries pointing at the same `repository_id` share a cache.

## Repository health state

A source that fails to load is currently recorded as a string error in
`failures()`. We make the *reason* explicit so the CLI and doctor can present a
precise status and so future logic (failover, parallel refresh) can decide what
is worth retrying.

```cpp
enum class RepositoryStatus {
    Available,        // loaded and verified successfully
    Unavailable,      // transient: could not reach the source
    NetworkError,     // connection refused / timeout / DNS / curl transport error
    Expired,          // repository.toml expires in the past
    InvalidSignature, // signature missing or does not verify against a trusted key
    InvalidMetadata   // repository.toml present but malformed / wrong format version
};
```

Mapping from the thrown `error::ErrorCode`:

| ErrorCode                | RepositoryStatus     |
|--------------------------|----------------------|
| (no error)               | `Available`          |
| `RepositoryNotFound`     | `Unavailable`        |
| `DownloadFailed`         | `NetworkError`       |
| `RepositoryExpired`      | `Expired`            |
| `InvalidSignature`       | `InvalidSignature`   |
| `TrustedKeyNotFound`     | `InvalidSignature`   |
| `InvalidRepository`      | `InvalidMetadata`    |
| `InvalidManifest`        | `InvalidMetadata`    |

`RepositoryManager` exposes, per loaded source, its `RepositoryStatus` so that
`meow sync` can print a table such as:

```
meow sync

core       ✓ updated
testing    ✗ timeout
local      ✓ unchanged
```

Semantics:

- `Unavailable` / `NetworkError` are **transient** and eligible for retry and
  failover (below). They must not poison the merged view.
- `Expired`, `InvalidSignature`, `InvalidMetadata` are **fatal for that source**
  and are reported, never retried, never failed over. A bad signature or checksum
  means the data cannot be trusted, regardless of how many mirrors serve it.
- A single non-`Available` source is a warning/check failure, not a hard error,
  as long as at least one source loaded (so the merged view is non-empty).

## Priority (documented rule)

Selection across configured sources uses **priority first, then version**:

1. For a package name, collect every source that provides it.
2. Pick the source with the **highest `priority`**.
3. If several sources tie on priority, pick the one with the **highest latest
   version** of that package.
4. That source's package record (all its versions, dependencies, conflicts,
   provides) enters the merged view. The other sources contribute nothing for
   that name.

## Selection algorithm

The merged view is built deterministically by `RepositoryManager::buildMerged()`
using the rule above. The exact procedure is fixed by contract and covered by the
integration tests in `test/integration.sh` (section 32. Repository priority
selection):

1. Filter repositories with **usable** metadata — sources whose load status is
   `Available`. Unavailable sources (network error, expired, bad signature,
   malformed metadata) are excluded from the merged view but remain visible in
   the health table.
2. **Order repositories by priority descending.** This is the primary key; it is
   independent of config order.
3. **Search candidates in repository order.** For each package name, the first
   repository (highest priority) that provides the package supplies the merged
   record for that name.
4. **Inside the selected repository, choose the highest satisfying version** of
   that package. Version is only ever a tie-breaker, never a reason to override
   priority.
5. **Never downgrade trust failures into fallback silently.** An
   `Expired` / `InvalidSignature` / `InvalidMetadata` source is a hard trust
   failure for that source; it is not retried and never used as a fallback, even
   when a lower-priority healthy source exists. Health state and resolver
   behavior stay strictly separate: a broken source is reported, not hidden, and
   the resolver simply does not see it.

This rule is stable and independent of config order. It is already implemented;
this section fixes it as the documented contract so future changes (mirror
groups, failover) cannot silently reorder it.

Default priority is `0`. Recommended convention for operators:

```toml
[[repositories]]
id = "main"
url = "https://mirror1.example"
priority = 100

[[repositories]]
id = "main-fallback"
url = "https://mirror2.example"
priority = 90
```

Here `mirror1` wins for every package it carries; `mirror2` is only consulted
for packages `mirror1` does not provide (because the merged view still pulls
missing names from the next source by priority).

## Mirror groups (same repository, multiple URLs)

**Not every URL is a separate repository.** Two URLs that carry the same
`repository_id` are mirrors of one logical repository. We model this explicitly
so priority, trust, and cache are computed once per repository, not once per
URL.

The model is now implemented. A configured source is one repository identity
with a `mirrors` list; the legacy single `url` form is migrated internally to a
one-element `mirrors` list. This separation is the data model; the runtime
failover policy (when to try the next mirror) is a separate, later step.

### Repository vs mirror

| Aspect            | Repository                                  | Mirror                              |
|-------------------|---------------------------------------------|-------------------------------------|
| Identity          | has a cryptographic `repository_id`         | none (transport location only)     |
| Signature         | has and verifies a signature                | none (inherits the repo's)          |
| Metadata          | owns the package metadata                   | none (serves the repo's)            |
| Cache             | one cache keyed by `repository_id`          | none (shares the repo's cache)      |

Because a mirror has no identity of its own, two mirrors of the same repository
must produce identical `repository_id`, signature, and metadata. A mirror that
diverges is a broken mirror, not a different repository.

Config:

```toml
[[repositories]]
id = "main"
mirrors = [
    "https://mirror1.example",
    "https://mirror2.example"
]
priority = 100
```

Semantics:

- A mirror group has one `repository_id`, one signature, one metadata set, one
  cache directory (keyed by `repository_id`).
- All mirrors in a group are expected to serve *identical* signed metadata and
  artifacts. Divergence is a mirror bug, not a feature.
- Priority is per *group*, not per URL. The group competes with other groups by
  the same priority-then-version rule.
- Within a group, URLs are tried in listed order for failover (below).
- The legacy `url = "..."` form remains valid and is exactly a group with a
  single mirror.

The merged view is built per group: a group contributes a package only if its
priority beats other groups (or ties and wins on version), exactly as today's
per-source rule, just operating on groups instead of bare URLs.

## Mirror failover

Mirrors are transport alternatives only.

- A mirror cannot provide different metadata.
- A mirror cannot override trust decisions.
- A failed verification stops selection.

Failover applies to *artifact downloads* (and, for mirror groups, to metadata
refresh) **within a group**. It is driven by transport-level problems only. The
policy is now implemented in `loadRepositoryWithFailover()`
(`meow/src/repository/failover.cpp`): for a configured source, mirrors are tried
in listed order; the first that loads and verifies wins. The decision of whether
to move to the next mirror is centralized in `isFailoverAllowed()`:

Attempt order for a metadata refresh from a group with mirrors `[A, B, C]`:

```
metadata refresh
       |
       v
mirror A --(ok)--> done
       |
    (transport failure)
       v
mirror B --(ok)--> done
       |
    (transport failure)
       v
mirror C --(ok)--> done
       |
    (transport failure)
       v
FAIL
```

Fail over (try the next mirror) **only** on:

- connection refused / connection reset
- DNS failure
- timeout
- HTTP `5xx`

Do **not** fail over on:

- HTTP `404` for `repository.toml` (or any metadata) — this means the mirror is
  missing expected data; surface it as a failure for the source.
- invalid signature
- expired metadata
- invalid `repository_id` / malformed metadata
- invalid checksum (`ChecksumMismatch`)

Those last ones indicate a bad or compromised mirror; failing over would only
land on another copy of the same (untrusted) data, and crucially must **never**
relax the trust check. A trust failure aborts the chain immediately (the next
mirror is **not** tried), and the source is reported with its trust status
(`InvalidSignature` / `Expired` / `InvalidMetadata`). The full attempt history is
preserved in `RepositoryState::attempts` so the health table can show every
mirror that was tried, never silently dropping a failed one.

Failover must be bounded: try each mirror at most once per refresh (no unbounded
fan-out), and respect the existing `downloadWorkers`/`hookTimeout`-style limits
where they apply. A per-mirror attempt timeout keeps a single dead mirror from
hanging the whole operation.

## Parallel metadata refresh

Only after health state, priorities, and failover are in place.

`meow sync` refreshes every configured group's metadata. Because each group's
load is independent and transport-bound, refresh runs in a pool that reuses the
existing download-worker logic:

```
meow sync

core      ──────┐
testing   ──────┼──> refresh pool (bounded workers)
local     ──────┘
```

Rules:

- Refresh is **concurrent** but **bounded** by the configured worker count
  (default reused from `downloadWorkers`).
- A group's failure status is recorded independently; one slow/dead group does
  not block the others.
- Within a group that has multiple mirrors, metadata refresh itself uses the
  failover order above (try mirror A, then B, ...), stopping at the first that
  returns valid, verifiable metadata.
- The merged view is rebuilt once after all refreshes settle, using the
  priority-then-version rule. A refresh that produced `InvalidSignature` /
  `Expired` / `InvalidMetadata` is excluded from the merged view but recorded in
  the health table.
- Parallel refresh never weakens trust: every refreshed metadata set still goes
  through signature verification against the trusted keys before it is admitted.

## Trust is non-negotiable

Across all of the above — priority, mirror groups, failover, parallel refresh —
the trust chain is unchanged and strict:

```
signature fetch -> trusted-key lookup -> verify -> expiry check
```

Availability concerns (a mirror being down, a group timing out) never cause
`meow` to skip or weaken signature verification, expiry checks, or checksum
validation. If a package can only be obtained from a source that fails trust,
the operation fails for that package; it does not fall back to an untrusted
mirror.

## Implementation order

1. `RepositoryStatus` enum + status mapping in `RepositoryManager` (health
   state). No behavior change to selection; purely observable.
2. Documented priority rule (already implemented; add tests, including
   memory-backend unit cases).
3. Mirror groups: `mirrors = [...]` config, group-as-source in the manager.
4. Failover on artifact download (transient-only; never on trust failures).
5. Parallel metadata refresh using the download-worker pool.

Each step ships with integration coverage and, where disk/network-free testing
helps, memory-backend unit tests.
