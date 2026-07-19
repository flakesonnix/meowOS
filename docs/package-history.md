# Package history

Package history answers the question *"why is this installed?"* — the missing
layer between package **identity** (the `packages` table) and the package
**graph** (resolver / SAT work that comes later). It is deliberately separate
from the package database: history is an append-only log, never the source of
truth for what is installed.

## Install reason

> **Install reasons are monotonic.** Once a package reaches a higher-precedence
> reason, later operations must not lower it. An operation may *promote* a
> reason but never *demote* it.

Precedence:

```
Explicit > GroupMember > Dependency
```

Every installed package carries a single **current reason**. A package has
exactly one database row, and its reason can only be *upgraded*, never
downgraded:

| Reason       | Meaning                                            | Priority |
|--------------|----------------------------------------------------|----------|
| `Explicit`    | the user ran `meow install foo`                    | 3 (highest) |
| `GroupMember` | installed through `meow group install <group>`     | 2 |
| `Dependency`  | pulled in transitively by the resolver             | 1 (lowest) |

Promotion table (current → new → result):

| Current     | New         | Result      |
|-------------|-------------|-------------|
| Dependency  | Explicit    | Explicit    |
| GroupMember | Explicit    | Explicit    |
| Dependency  | GroupMember | GroupMember |
| Explicit    | GroupMember | Explicit    |
| Explicit    | Dependency  | Explicit    |

This invariant becomes critical once `autoremove` exists: a package that was
ever explicitly requested must never be pruned as an orphaned dependency.

When the same package is reached by two paths (e.g. `hello` is `Explicit`, then
later part of a group install), the stronger reason wins and the weaker one is
ignored. `registerPackage` never overwrites the reason on upgrade — it is set
separately via `setInstallReason`, which is upgrade-only.

The reason is stored as `packages.install_reason` so `meow why` and
`meow explicitly-installed` work without scanning history.

## History (append-only log)

```
package_history
--------------
id            INTEGER PRIMARY KEY
timestamp     INTEGER NOT NULL   -- unix seconds
action        TEXT NOT NULL      -- "install" / "remove"
package       TEXT NOT NULL
version       TEXT
reason        TEXT               -- string form of InstallReason at action time
transaction_id TEXT             -- groups one atomic transaction's rows
```

History is **never edited after insertion**. Old rows are preserved so the log
is a faithful audit trail; the `packages` table remains the source of truth.

## Transaction IDs

Each install runs inside one atomic transaction (`meow::transaction`). The
transaction generates a UUID (`transaction_id`) that is stamped onto every
history row it produces, so a single `meow install app` that pulls `app` +
`libfoo` + `hello` appears as one grouped, auditable event.

```
transaction abc123
  + app       (Explicit)
  + libfoo    (Dependency)
  + hello     (Dependency)
```

`meow history` prints the transaction id per row, turning history into a
debugging tool.

## Commands

```
meow history                 # all actions, oldest first
meow history <package>       # actions for one package

meow why <package>           # current reason + what requires it
meow explicitly-installed    # every package with reason == Explicit
```

`meow why` currently reports the package's reason and the packages that
`required-by` it. The richer "provided by virtual package X" output is future
work once the resolver graph is richer (see roadmap).

`meow explicitly-installed` is the pre-flight list for a future
`autoremove`: only packages whose reason is `Explicit` (or `GroupMember`) are
kept; orphaned `Dependency` packages can be pruned.

## Examples

```
$ meow install gcc
$ meow why gcc
gcc 13.0.0
reason: explicit
required by:
  (nothing)

$ meow group install base-devel
$ meow why gcc
gcc 13.0.0
reason: explicit          # explicit is preserved through group install
required by:
  (nothing)

$ meow history
2026-07-19 18:00 install gcc 13.0.0
  reason: explicit
  transaction: a1b2c3d4-...
2026-07-19 18:02 install make 4.4.0
  reason: group
  transaction: e5f6...
```

## Migration

Databases at schema v1 (no `install_reason` / `package_history`) are migrated
on open:

- `packages` gains the `install_reason` column (default `Dependency`).
- `package_history` and the `files` / `package_deps` / `package_provides`
  tables are created if absent.

Existing packages default to `Dependency` until a reason is next recorded; this
is a safe default because it never claims a package was explicitly requested.

## Out of scope

This milestone does **not** include:

- automatic removal of unused dependencies (`autoremove`)
- rollback to a previous transaction
- dependency-graph reconstruction from history
- SAT solver integration

Those depend on the richer resolver model and will build on this history layer.
