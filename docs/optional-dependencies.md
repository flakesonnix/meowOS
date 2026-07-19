# Optional dependencies

Optional dependencies describe packages that *enhance* another package but are
not required for it to function. They are declared in a package manifest as a
list of `[[optional_depends]]` tables, each with a `package` name and an
optional `description`:

```toml
[[optional_depends]]
package = "gtk4"
description = "GUI support"

[[optional_depends]]
package = "qt6"
description = "Qt frontend"
```

This document is the **specification** for optional-dependency behavior. The
format and UI are defined here; the resolver and installer must follow it.

## Design principle: metadata first, behavior later

The manifest field and `meow info` output shipped first, with **no** install
behavior. Optional dependencies are *metadata* until the user explicitly opts
in at the command line. This keeps the parser, repository format, and UI stable
before the resolver knows anything about optional packages.

## The key question: when does an optional dependency become "explicit"?

> **An optional dependency the user asks for is recorded as `Explicit`, not
> `Dependency`.**

The command line — not whether a name appeared under `depends` or
`optional_depends` — determines the install reason. If the user explicitly
requests an optional component, they own that decision, and later maintenance
commands must respect it.

```
meow install hello --with-optional
```

installs `hello` plus every `optional_depends` entry (`gtk4`, `qt6`), recording:

```
hello   reason = Explicit
gtk4    reason = Explicit     # user asked for optional components
qt6     reason = Explicit     # user asked for optional components
```

Likewise:

```
meow install hello --optional gtk4
```

records `gtk4` as `Explicit` (and only `gtk4`, not `qt6`).

### Why not a fourth reason (`Optional`)?

The existing reason lattice is sufficient:

```
Explicit > GroupMember > Dependency
```

Adding an `Optional` tier would complicate the monotonic invariant (see
[package-history.md](package-history.md)) and the future `autoremove` logic for
no benefit. A user-requested optional package is indistinguishable in intent
from a user-requested required package: both are explicit choices. The only
difference is *how* the user discovered the name (the manifest suggested it).

### Consequence for `autoremove`

Because chosen optional packages are `Explicit`, a later:

```
meow autoremove
```

must **not** remove them. They are protected exactly like any other explicitly
installed package.

## Phase 2: install behavior

Two opt-in flags, no interactive UI, no prompting.

### `meow install <pkg> --with-optional`

1. Resolve the normal dependency closure of `<pkg>`.
2. Append **every** `optional_depends` entry of `<pkg>` (and transitively of
   any package in the closure that declares optionals).
3. Resolve the combined set again.
4. Install everything in **one atomic transaction**, recording each requested
   optional as `Explicit`.

### `meow install <pkg> --optional <name>[,<name>...]`

Same as `--with-optional`, but only the **named** optional dependencies are
appended (must be declared as optionals of a package in the closure, otherwise
rejected). Each selected optional is recorded as `Explicit`.

### Out of scope (phase 2)

- No interactive selection / prompts.
- No automatic installation of optionals unless the flag is given.
- No new install reason.
- No change to the monotonic invariant.
- No `autoremove` implementation (depends on this recording to be correct).

## Future

After optional install lands, the roadmap continues with virtual-package
improvements, then **resolver diagnostics** (`meow explain <pkg>`,
`meow why-not <pkg>`) *before* any SAT solver work. Those diagnostic commands
are the validation harness for the solver and should exist before SAT.
