# Hooks (restricted hook runner)

Package scripts (`pre_install`, `post_install`, `pre_remove`,
`post_remove`) are executed by a controlled **hook runner** rather than a
bare `system()`. The runner isolates the script and bounds its runtime.

```
package
  │
  ▼
hook runner
  ├── environment   (minimal, controlled)
  ├── timeout       (SIGTERM → SIGKILL)
  ├── cwd           (isolated staging dir)
  ├── logging       (output captured via log::info)
  └── policy        (network advisory)
```

## Isolation

- **Working directory** — the script runs in an isolated staging dir
  (`$TMPDIR/meow/hooks/<package>/<type>/`), never the system root.
- **Environment** — by default a minimal env is used:
  - `HOME=$TMPDIR/meow/hook-home`
  - `PATH=/usr/bin:/bin`
  - `MEOW_PACKAGE`, `MEOW_VERSION`, `MEOW_HOOK_TYPE`, `MEOW_HOOK_STAGING`
  - Set `hooks.inheritEnvironment = true` (config) to pass the ambient
    environment through instead.
- **Output** — stdout/stderr are captured and re-emitted through
  `log::info` (prefixed with the hook name), not dumped raw.
- **Timeout** — each hook is killed after `hookTimeout` seconds
  (default 30). On expiry the process receives `SIGTERM`; if it does not
  exit within 2s it is `SIGKILL`ed. The install then fails with
  `HookTimeout`.

## Policy

Network isolation is **advisory** in this version. When
`hooks.allowNetwork = false` (the default) but the OS provides no
enforcement mechanism, the runner emits a warning — it never silently
allows unrestricted network access. Strong isolation (namespaces /
seccomp / containers) is a follow-up.

## Error codes

| Code         | Trigger                                      |
|--------------|----------------------------------------------|
| `HookFailed` | Script exited with a non-zero status         |
| `HookTimeout`| Script exceeded the configured timeout       |
| `HookDenied` | Script file missing / not executable         |

A failed pre-install or post-install hook aborts the install and the
transaction is rolled back (extracted files are removed).

## Configuration

```toml
[hooks]
timeout = 30        # seconds
network = false     # advisory; requires OS support to enforce
```

(Loaded via `meow` config; `inheritEnvironment` is also configurable.)
