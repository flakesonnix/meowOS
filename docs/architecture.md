# Architecture

meowOS is a transactional Linux package manager. This document sketches the
core data flow; module headers under `meow/include/meow/*` are authoritative.

## Chain of custody

```
source tree
    │  meow-build
    ▼
deterministic artifact (.pkg.tar.zst)     ← reproducible builds
    │  sha256
    ▼
repository metadata (by-name layout)      ← sharded, signed (Ed25519)
    │  signature + repository_id + expiry
    ▼
signed package index (packages.toml.sig)  ← authenticates every manifest + artifact hash
    │
    ▼
trusted key store (~/.config/meow/keys/)  ← trust anchor
    │
    ▼
lockfile (meow.lock)                      ← pinned hash + version
```

## Install flow

```
meow install <pkg>
    │
    ▼
config + database open
    │
    ▼
    repository open  ──► signature verify ──► signed index verify ──► identity ──► expiry
    │
    ▼
resolveDependencyNames        ← repo metadata only, no downloads
    │  (SAT: DPLL over CNF, version constraints, UNSAT diagnostics)
    │  (Legacy: topological, cycle detection, no version constraints)
    ▼
downloadAll (parallel)        ← bounded worker pool, atomic .part + rename
    │  retries / resume / ETag / timeout / size guard
    ▼
resolvePackage (cache hit)    ← load metadata + verify sha256
    │
    ▼
transaction begin
    │  for each package:
    │     pre_install hook  ──► extract ──► post_install hook
    │  on any failure → rollback (remove extracted files)
    ▼
commit → register in SQLite database
```

## Module map

| Area            | Module                    | Responsibility                              |
|-----------------|---------------------------|---------------------------------------------|
| CLI             | `main.cpp`                | command dispatch, config/db/repo wiring    |
| Config          | `config`                  | paths, repositories, workers, hook policy   |
| Crypto          | `crypto`                  | Ed25519 signatures + trusted key store      |
| Repository      | `repository`              | by-name scan, open/verify, index verify     |
| Resolver (SAT)  | `dependency/sat`          | DPLL over CNF, version constraints, UNSAT   |
| Resolver (Legacy)| `dependency/legacy`      | DFS-based, cycle detection, compatibility   |
| Resolver factory| `dependency`              | `Auto → Sat` routing, factory switch       |
| Download        | `download`                | libcurl transport, atomic writes, retries   |
| Download queue  | `download/queue`          | bounded parallel fetch of artifacts         |
| Builder         | `builder`                 | reproducible `.pkg.tar.zst` generation       |
| Archive         | `archive`                 | libarchive read/extract                     |
| Transaction     | `transaction`             | begin/record/commit/rollback                 |
| Install         | `install`                 | installPackages + hook runner               |
| Hooks           | `hooks`                   | isolated, timed, logged script execution    |
| Database        | `database`                | SQLite package/file registry + history       |
| Lockfile        | `lock`                    | reproducible install pinning                |
| Verify/Repair   | `verify`, `repair`        | integrity check / restore                    |
| Sync/Update     | `sync`, `update`          | upstream diff / bulk upgrade                 |
| Doctor          | `doctor`                  | system + security diagnostics               |
| Repo builder    | `repo-builder`            | repo sign + package index generation        |

## Layering

Dependency arrows point from a higher layer to the module it depends on.
Lower layers must not depend on higher ones (e.g. the database never knows
about HTTP; the downloader never parses manifests).

```
CLI (main.cpp)
    │  config + database open
    ▼
Repository  ──── open / verify signature / verify package index / identity / expiry / cache
    │
    ├── FilesystemRepositoryBackend   (local checkout)
    ├── HttpRepositoryBackend         (remote server)
    └── MemoryRepositoryBackend       (tests only)
    │
    ▼
Resolver  ──── SAT (DPLL over CNF) or Legacy (DFS) / metadata only
    │
    ▼
Downloader / Download queue  ──── libcurl transport, atomic writes, retries
    │
    ▼
Transaction  ──── begin / record / commit / rollback
    │
    ├── Hooks            (isolated, timed, logged; Hook ABI v1)
    ├── Archive extract  (libarchive)
    └── Database         (SQLite package/file registry + history)
    │
    ▼
Lockfile / Verify / Repair / Sync / Update / Doctor  (operate on DB + cache)
```

The resolver, dependency solver, install, and sync paths all talk to the
`Repository` abstraction and must not care whether data came from a local
checkout or a remote backend. Adding mirrors, object storage, or OCI
registries later means adding a backend, not branching through the core.

### Layering rules

- `database`, `transaction`, `hooks`, `archive` must not reference
  `download` or `repository` URL schemes.
- `download` moves bytes only; it does not parse package/repo manifests.
- `repository` owns transport selection (backend) and metadata parsing; the
  rest of the code consumes an in-memory `Repository`.
- `CLI` is the only place that wires config → db → repo → commands.

## Design invariants

- **Repository metadata is the source of truth** for resolution; downloads
  are independent of verification/installation.
- **Install is strictly serial** even when artifact downloads are parallel,
  so the transaction/DB path stays deterministic.
- **Verification never trusts the cache as a source**: an artifact is
  re-checked against the repository-declared sha256 after download.
- **Hooks are isolated, not sandboxed**: a controlled runner bounds
  runtime and environment; OS-level sandboxing (namespaces/seccomp) is a
  planned follow-up.
