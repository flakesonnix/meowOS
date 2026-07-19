# Doctor

`meow doctor` runs a read-only diagnostic sweep over the local meow
installation and reports problems. It reuses the existing systems
(config, database, repository, lockfile, verifier) and never mutates
state.

```bash
meow doctor          # human-readable report
meow doctor --json   # machine-readable report for bug reports / CI
```

The command exits non-zero when one or more checks report an error
(`healthy: false`).

## Checks

| Category      | Check                     | Failure modes                                        |
|---------------|---------------------------|------------------------------------------------------|
| `config`      | repositories configured   | no repositories listed                               |
| `database`    | schema present            | missing/corrupt schema tables (warning if not yet created) |
| `repository`  | signature trusted         | repo could not be opened (bad/!expired/!identity)    |
| `repository`  | repository identity        | missing `repository_id`                              |
| `repository`  | metadata not expired      | expired or unparseable `expires`                     |
| `cache`       | cache directory           | missing (warning) or not writable (error)            |
| `cache`       | verified metadata cache   | no cached metadata for this `repository_id` (warning) |
| `lockfile`    | lockfile consistency      | locked versions differ from installed (warning); unparseable (error) |
| `integrity`   | installed file integrity  | missing/modified files (warning)                     |
| `disk`        | free space                | low (<0.5 GB error, <2 GB warning)                   |
| `permissions` | writability               | root or key store not writable (error)               |

## Notes

- The repository is opened inside a `try/catch`: if trust/expiry/identity
  validation fails, `doctor` still runs and reports the failure as a check
  rather than aborting — this is deliberate so it can *diagnose* broken
  repositories.
- Logs are suppressed to `Error` level while `doctor` runs so the report
  is not drowned in `[INFO]` noise.

## `meow doctor --security`

A read-only, security-focused variant. It never repairs or mutates state
(that is left to `meow repair --security`, a future command). Checks:

| Category    | Check                      | Notes                                          |
|-------------|----------------------------|------------------------------------------------|
| `keys`      | trusted keys configured    | key store exists + non-empty + readable        |
| `repository`| signature valid            | signed by a trusted key                        |
| `repository`| identity matches cache     | `repository_id` consistent with cache dir      |
| `repository`| metadata not expired       | `expires` in the future                        |
| `cache`     | no stale partial downloads  | no `.part` leftovers                            |
| `cache`     | no zero-size artifacts     | no truncated cached files                      |
| `lockfile`  | lockfile artifacts verified | every entry has artifact hash + version        |
| `lockfile`  | repository identity recorded| `repositoryHash` present                       |
| `hooks`     | hook policy loaded         | timeout / network / environment reported       |
| `hooks`     | hook network isolation     | advisory: OS-level enforcement not yet present |

This gives a single summarized view of the chain of custody:

```
source → meow-build → deterministic artifact → sha256 →
repository metadata → signature → lockfile
```

