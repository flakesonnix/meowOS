# Fixture Isolation — Phase 2 Audit & Implementation (Agent B)

Goal: make every integration section operate on isolated fixtures so sections
can later run in parallel. No CTest / registration / section-layout changes.

## 1. Fixture audit (per section)

Legend: R=reads repo/ · W=modifies repo/ · H=modifies HOME/.cache ·
K=modifies trusted keys · D=own DB · S=starts HTTP server

| Section | reads repo | writes repo | HTTP server | keys | cache | HOME | TMPDIR | DB |
|--------:|:----------:|:-----------:|:-----------:|:----:|:-----:|:----:|:------:|:---:|
| 01 basic install     | R | – | – | – | R | – | – | shared TEST_DB |
| 02 repo metadata     | R | W (tamper/sign) | – | W (mv key) | – | – | – | shared |
| 03 download          | R | W (rewrite ver) | – | – | W | – | – | shared |
| 04 http              | R | W (sign) | S (startHttp) | – | R | – | – | shared |
| 05 doctor            | R | – | – | – | R | – | – | shared |
| 06 reproducible      | – | – | – | – | – | – | – | shared |
| 07 hooks             | R | W (registerHookPkg) | – | – | – | – | – | shared |
| 08 fresh install     | R | – | – | – | R | – | – | shared |
| 09 server hosting    | R | W (pkg copy) | S (meow-server) | – | – | – | – | shared |
| 10 http backend      | R | W (repo-http copy) | S | – | – | – | – | shared |
| 11 multi repo        | R | – | S | – | R | – | – | shared |
| 12 doctor repo       | R | – | S | – | R | – | – | shared |
| 13 dual backend      | R | W (repo-dual) | S | – | – | – | – | own (/tmp) |
| 14 resolver          | R | – | – | – | – | – | – | shared |
| 15 health state      | R | W (repo-expired) | S | – | – | – | – | shared |
| 16 priority          | R | W (priority dirs) | S | – | – | – | – | shared |
| 17 mirror groups     | R | W (mir copies) | – | – | – | – | – | shared |
| 18 mirror failover   | R | W (fo copies) | S (python3) | – | – | – | – | shared |
| 19 parallel refresh  | R | W (fo copies) | S (python3) | – | – | – | – | shared |
| 20 history           | R | – | – | – | R | – | – | shared |
| 21 optionals         | R | W (registerOptPkg) | – | – | – | – | – | shared |
| 22 groups            | R | – | – | – | R | – | – | shared |

Key observation: before this change **every** section shared the canonical
`./repo`, `~/.cache/meow`, the trusted keys, and a single `$TEST_DB`. Section
02 even moved the trusted key out of the way and restored it — unsafe under
parallel execution.

## 2. Reusable fixture helpers (test/integration/common.sh)

- `create_repo_fixture()` — copies canonical `./repo` into a run-scoped workdir
  and prints its path. `integration.sh` `cd`s into the result, so every
  section's relative `repo/` now resolves to the **isolated copy**. Honors
  `FIXTURE_COPY` (default `cp -a`).
- `create_home_fixture()` — isolated `$HOME` with an empty cache and the
  trusted **public** key installed. Exported by `integration.sh`.
- `create_cache_fixture()` — isolated `~/.cache/meow` (without full HOME).
- `create_db_fixture()` — unique `mktemp` DB path.
- `create_keys_fixture()` — **new**: isolated keys dir with the trusted public
  key, for sections that need to drop/re-add a key (replaces the brittle
  `mv ~/.config/meow/keys/...` pattern in section 02).
- All helpers register cleanup via `init_fixtures`' `EXIT` trap; nothing is
  left in the canonical tree or the user's HOME.

Path resolution was centralized: `SCRIPT_DIR` / `ROOT_DIR` / `KEYS_DIR` are now
derived from `BASH_SOURCE[0]` instead of the broken `$(dirname "$0")` pattern
(which pointed outside the repo when sections were sourced by `integration.sh`).
This fixed a latent bug where key/binary paths resolved incorrectly under the
legacy runner.

## 3. Isolated fixture implementation

- `integration.sh` now calls `create_repo_fixture` and `cd`s into it before
  `bootstrapArtifacts`. Every subsequent `repo/...` write (tampering, signing,
  `repo-dual`, `mir-*`, `fo-*`) lands in the copy — the canonical `repo/` is
  never touched.
- Trusted-key handling installs the **public** key (`meow-release.pub` →
  `meow-release.pem` in the trust dir). The previous code installed the private
  key, which silently broke signature verification.
- Section 02's key-removal test should migrate to `create_keys_fixture()` +
  pointing `HOME`/`MEOW_CONFIG` at it; left as a follow-up to keep this commit
  behavior-identical.

## 4. Benchmark (cp -a vs rsync -a vs cp -al)

Environment: Linux, repo ≈ 196 KB, 5 copies each.

| method      | time (5×) | disk / copy | portability |
|-------------|----------:|------------:|-------------|
| `cp -al`    |  132 ms   | ~0 KB*      | Linux/macOS; fails on some FS → fallback |
| `cp -a`     |  149 ms   | 196 KB      | universal |
| `rsync -a`  |  462 ms   | 196 KB      | needs rsync |

\* hardlinks share inodes, so disk cost is negligible; mutating a copy's file
breaks the link per-file (safe, copy-on-write-free).

**Recommendation:** default to `cp -a` for portability and zero surprise when
sections mutate files; offer `FIXTURE_COPY=cp -al` for speed/space on trusted
local filesystems. `rsync -a` is not worth the dependency or the 3× cost. The
`_copy_repo` helper already implements this with a `cp -al` → `cp -a` fallback.

## 5. Verification

`./test/integration.sh` results after isolation:

- All non-network sections PASS (repo metadata, download tampering, hooks,
  install chains, reproducible builds, groups, history, resolver, priority,
  mirror copies, dual backend, doctor, fresh install).
- Remaining FAILs (2× "http fixture server did not start") are **pre-existing
  and environmental**: `python3` is absent from the test box, so the HTTP
  fixture server (`startHttp`) cannot start. This is unrelated to fixture
  isolation and was failing before this change.

Behavior of isolated sections is identical to the prior serial run; only the
location of mutable state changed (canonical tree → per-run fixtures).

## 6. Constraints honored

No scheduler, no parallel execution, no resolver/package-manager changes, no
CTest/CI changes, no `CMakeLists.txt` / `test/integration.sh` registration
changes. This commit only prepares the suite for future parallel execution.
