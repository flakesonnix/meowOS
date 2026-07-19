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

## Error codes

| Code                  | Trigger                                        |
|-----------------------|------------------------------------------------|
| `DownloadFailed`      | General curl/transport failure after retries   |
| `DownloadTimeout`     | Transfer exceeded the configured timeout       |
| `DownloadHttpError`   | Terminal HTTP error (`4xx`/`5xx` after retries) |
| `DownloadInterrupted` | Size cap exceeded (`maxBytes`)                 |
| `InvalidDownload`     | Unsupported scheme or missing file:// source   |

## Checksums

After download, the archive SHA256 is verified against the value in
repository metadata. A mismatch throws `ChecksumMismatch` and deletes the
bad file.
