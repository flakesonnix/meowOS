# Downloads

MeowOS fetches package archives over `file://` and `http(s)://` URLs
declared in repository version metadata (`artifact.url`). Transfers use
**libcurl** directly (no shell execution).

## Transport model

- `file://` sources are copied atomically (no network).
- `http(s)://` transfers go through libcurl with:
  - TLS verification on by default (`verifyTls = false` disables it for
    internal/testing only),
  - redirect following (up to 10 hops),
  - configurable connect + total timeout,
  - retries on transient failures.

## Robustness

- **Atomic downloads** — content is written to `<destination>.part` and
  renamed into place only on success. A failed or interrupted download
  never leaves a half-written file at the final path, and any existing
  valid cache file is left untouched.
- **Retries** — transient errors are retried `retries` times (default 3)
  before failing:
  - *Retry:* connection refused/reset, DNS failure, timeouts,
    `CURLE_GOT_NOTHING`/`RECV`/`SEND` errors, HTTP `5xx`.
  - *No retry:* checksum mismatch, invalid URL scheme, HTTP `4xx`
    (including `404 Not Found`).
- **Content-Length guard** — when `maxBytes > 0`, the declared
  `Content-Length` (HTTP) or file size (file://) is checked up front; an
  oversized response (or one exceeding the cap mid-stream) aborts with
  `DownloadInterrupted`. This rejects broken or intentionally huge
  downloads early, before they hit disk — important for GB-scale
  `.tar.zst` packages.
- **ETag passthrough** — an optional `etag` is sent as an
  `If-None-Match` header. A `304 Not Modified` response reuses the
  existing cached file without re-downloading.

## Progress UI

`downloadAll` prints a single-line colored progress bar per artifact:

```
  → [ 3/27] ████████░░░░░░░░░░░░░░░░░░░░░░ 28% bash-5.3.pkg.tar.zst
```

The bar uses `█`/`░` with `36m`/`33m`/`90m` ANSI and `done/total` counters;
the line is cleared (`\r` + spaces) on batch completion. `installPackages`
prints stepped progress to `stdout`:

```
  [1/27] Installing filesystem 1.0.0
  [2/27] Installing glibc 2.42
  ...
  ✓ Transaction committed
```

On failure: `  ✗ Transaction failed, rolling back` → `stderr`. `log::` itself
now defaults to `Warning` and always writes to `stderr` (was `Debug` → `stdout`),
so `stdout` stays clean for machine-readable consumers.

## Error codes

| Code                  | Trigger                                        |
|-----------------------|------------------------------------------------|
| `DownloadFailed`      | General curl/transport failure after retries   |
| `DownloadTimeout`     | Transfer exceeded the configured timeout       |
| `DownloadHttpError`   | Terminal HTTP error (`4xx`/`5xx` after retries) |
| `DownloadHttp5xx`     | HTTP `5xx` specifically (retryable, triggers mirror failover) |
| `DownloadInterrupted` | Size cap exceeded (`maxBytes`)                 |
| `InvalidDownload`     | Unsupported scheme or missing file:// source   |
| `AlreadyLocked`       | `InstallLock` held by another `meow` process   |
| `BuildFailed`         | `meow-build` phase returned non-zero           |

## Parallel downloads

When you run `meow install <pkg>` (or `meow bootstrap`, which installs `base`
idempotently), meow first resolves the **entire dependency closure** from
repository metadata alone (no network access), skipping already-installed
same-version packages (idempotent resolvers via `installedVersion`).
It then fetches every required artifact **concurrently** using a bounded
worker pool (`meow/download/queue.hpp`):

- Worker count = `download_workers` config (default `0` →
  `min(hardware_concurrency, 8)`).
- Downloads are independent of verification and installation; only the
  artifact bytes are fetched in parallel. Progress is shown inline (see above).
- **Installation remains strictly serial** — packages are still verified
  and written to the filesystem one at a time, in dependency order, with
  ` [N/M] Installing` lines.
- On any download failure the batch is cancelled: remaining tasks are not
  started, in-flight transfers are joined, and leftover `.part` files are
  removed. The first error is reported (`DownloadFailed`,
  `DownloadHttpError`/`DownloadHttp5xx`, `DownloadTimeout`, `AlreadyLocked`, ...).

This separation keeps the transaction/install path single-threaded and
deterministic while still parallelizing the slow network step. Re-running an
install for satisfied deps short-circuits at resolve time (no downloads).

## Checksums

After download, the archive SHA256 is verified against the value in
repository metadata. A mismatch throws `ChecksumMismatch` and deletes the
bad file.
