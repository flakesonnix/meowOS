# Package groups & meta-packages

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
# repo/groups/editors.toml — file-based group (preferred)
name = "editors"
packages = ["vim", "emacs", "nano"]
```

## Meta-packages vs groups

**`base` is now a meta-package, not a group.**

- **Before:** `base` was a group: `meow.toml` `[[groups]] name="base"` and later
  `repo/groups/base.toml` (29 packages). `meow group install base` expanded the
  member list.
- **Now:** `base` is a real package at `pkgs/by-name/ba/base/package.toml`
  (v1.0.0, 27 `depends`). `meow bootstrap <root>` installs it by default (no
  `--group` flag since `feat(bootstrap): default to base meta-package`). The
  resolver treats it like any other package, and **idempotency** applies:
  re-running `meow bootstrap` or `meow install base` skips already-installed
  same-version deps via `installedVersion` checks in both resolvers.

`repo/groups/base.toml` was deleted in `5802214`. File groups remain supported
for other shareable sets, but the canonical way to ship a curated set like
`base` is a meta-package so it participates in versioning, signing (`packages.toml`),
and dependency resolution. `base-devel` stays at `pkgs/base-devel/src/package.toml`
as a separate meta for the build toolchain.

## Commands

```
meow group list                 # print every defined group and its members
meow group install base-devel   # expand and install all members atomically
meow bootstrap <root>           # installs base meta-package by default (idempotent)
meow install base               # same as above, but via explicit package name
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

## Bootstrap note

`base` via meta-package means `meow bootstrap` and `meow install` share the
same idempotent resolver path (`ResolveRequest::db` + `installedVersion`):
no special-case `--group` flag, no group-specific transaction logic. `--force`
is required for `meow bootstrap` into a non-empty target.

## Out of scope (deferred)

These are intentionally *not* part of the initial groups feature:

- recording an "installed reason" (`explicit` / `dependency` / `group`) in the
  database — a later `package history` phase can add this without changing the
  group model.
- groups as removable units (`meow remove base-devel`). Today you remove the
  individual member packages; a future phase may track membership for bulk
  removal.
- nested groups and version-pinned group members.
- `base` is no longer a group; use the `base` meta-package. File groups
  (`repo/groups/*.toml`) remain for non-base shareable sets.
