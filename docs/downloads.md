# Downloads

MeowOS fetches package archives over `file://` and `http(s)://` URLs
declared in repository version metadata (`artifact.url`).

## Robustness guarantees

- **Atomic downloads** — content is written to `<destination>.part` and
  renamed into place only on success. A failed or interrupted download
  never leaves a half-written file at the final path.
- **Resume** — HTTP transfers pass `-C -` to curl, resuming from a partial
  `.part` when the server supports `Range`.
- **Retries** — transient HTTP errors are retried (3 attempts, 1s delay).
- **Timeouts** — `DownloadOptions::timeout` applies to both connect and
  total transfer time (default 30s).
- **TLS** — verification is on by default; `--insecure` (or
  `verifyTls = false`) disables it for internal/testing use only.
- **ETag passthrough** — an optional `etag` is forwarded as an
  `If-None-Match` header, ready for `304 Not Modified` caching once
  repository metadata caching is wired to HTTP transport.

## Failure handling

Any non-zero transfer result throws `DownloadFailed` with the curl exit
code and the offending URL. The partial file is removed before the error
is raised, so retry or clean state is safe.

## Checksums

After download, the archive SHA256 is verified against the value in
repository metadata. A mismatch throws `ChecksumMismatch` and deletes the
bad file.
