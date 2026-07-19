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
