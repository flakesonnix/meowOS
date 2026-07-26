# Package groups

A *package group* is a named, local expansion alias over a list of package
names. Groups can be defined in two places:

1. **Config groups** — inline in `meow.toml` as a `[[groups]]` array.
2. **File groups** — as TOML files under `repo/groups/<name>.toml`.

File-based groups are preferred for shareable, repository-distributed group
definitions. Config groups take precedence if a name collision occurs.

```toml
# meow.toml — config groups (backwards-compatible)
[[groups]]
name = "base-devel"
packages = ["gcc", "make", "binutils"]
```

```toml
# repo/groups/base.toml — file-based group (preferred)
name = "base"
packages = ["filesystem", "glibc", "bash", "coreutils", "gcc"]
```

## Commands

```
meow group list                 # print every defined group and its members
meow group install base-devel   # expand and install all members atomically
```

## Invariants

Groups are expansion aliases, not package identities.

- `meow group install base-devel` expands to `gcc`, `make`, `binutils` and
  installs them through the same resolver/transaction path as
  `meow install`. It does **not** create a synthetic "group" entity in the
  database. The database records the individual packages:
  `gcc installed`, `make installed`, `binutils installed`.
- A group install is **atomic**: the entire expansion is resolved into one
  dependency closure, all artifacts are downloaded in parallel, and the whole
  set is committed in a single transaction. Either the group installs
  completely, or (on any failure) it changes nothing. It is *not* implemented as
  a loop of per-package installs, which would make rollback complicated.
- `meow install <pkg>` and `meow group install <grp>` share the exact same
  staging path (`resolveAndStage` → parallel download → `installPackages`), so
  behavior and failure semantics can never diverge.

## Validation

The config loader rejects malformed groups strictly:

- empty group name → error
- duplicate group name → error
- empty package list → error
- a group name that collides with a reserved CLI command (`install`, `remove`,
  `update`, `group`, ...) → error, so the CLI surface never becomes ambiguous

## Out of scope (deferred)

These are intentionally *not* part of the initial groups feature:

- recording an "installed reason" (`explicit` / `dependency` / `group`) in the
  database — a later `package history` phase can add this without changing the
  group model.
- groups as removable units (`meow remove base-devel`). Today you remove the
  individual member packages; a future phase may track membership for bulk
  removal.
- nested groups and version-pinned group members.
