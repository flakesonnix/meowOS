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
- **No inheritance** — when not inheriting, the runner calls `clearenv()`
  and `execve()` with only the variables listed below. The hook process
  *cannot* see any variable from the builder (CI secrets, `GITHUB_*`,
  `NIX_*`, `SSH_AUTH_SOCK`, `XDG_*`, cloud credentials, API tokens, …).
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

## Hook ABI (v1)

The hook environment is a **stable, versioned contract**. Package authors
may rely on exactly the variables listed below; anything not listed here is
intentionally absent and must not be used. The current version is **v1**.

**Versioning policy.** The ABI version is incremented whenever a *new
guaranteed* variable, working-directory rule, or stdio behavior is added, or
when an existing guarantee changes. Existing guarantees are never changed
silently: a behavioral change is always a new ABI version, not an in-place
edit of v1. When the runner supports more than one version, it advertises
the highest version it implements so package authors can target a known
contract. A future `meow` may emit a warning if a hook script relies on an
unversioned assumption that the running ABI does not guarantee.

### Environment

| Variable            | Value                                            |
|---------------------|--------------------------------------------------|
| `HOME`              | `$TMPDIR/meow/hook-home`                         |
| `PATH`              | `/usr/bin:/bin`                                  |
| `TMPDIR`            | the system temp directory                        |
| `MEOW_PACKAGE`      | package name being acted on                      |
| `MEOW_VERSION`      | package version being acted on                   |
| `MEOW_HOOK_TYPE`    | `pre_install` / `post_install` / `pre_remove` / `post_remove` |
| `MEOW_HOOK_STAGING` | absolute path to the isolated staging directory  |

The runner **guarantees** the variables listed above. The `/bin/sh`
interpreter may also set shell-specific variables such as `PWD`, `SHLVL`,
and `_`; these are **not** part of the stable Hook ABI and their presence
or absence depends on the system shell (dash, bash, busybox, …). Package
authors must not rely on them. The isolation guarantee is that no
builder/CI variable (e.g. `CI`, `GITHUB_*`, `NIX_*`, `SSH_*`, `XDG_*`) can
leak into the hook.

When `hooks.inheritEnvironment = true`, the builder's ambient environment is
passed through in addition to (and overriding) the variables above.

### Working directory

The staging directory: `$TMPDIR/meow/hooks/<package>/<type>/`. The script
never runs against the system root.

### stdio

- `stdin` — `/dev/null`
- `stdout` / `stderr` — captured and re-emitted through `log::info`
  (prefixed with the hook name)

### Network

Advisory only in this version (see Policy). Treat hooks as if they have
network access until OS-level isolation (namespaces / seccomp) lands.

### Adding to the ABI

Any new variable or behavior change must be an intentional, documented
revision of this ABI. The integration test asserts the *exact* environment
variable set, so accidental additions are caught in CI.

## Configuration

```toml
[hooks]
timeout = 30        # seconds
network = false     # advisory; requires OS support to enforce
```

(Loaded via `meow` config; `inheritEnvironment` is also configurable.)
