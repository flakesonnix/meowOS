#!/usr/bin/env bash
# Permanent security regression suite for meowOS.
#
# Runs entirely against the built binaries (meow, meow-build, meow-repo) and the
# C++ security unit tests. It does NOT modify production code; it only exercises
# the existing trust / extraction / hook / transaction behavior and asserts the
# secure outcome.
#
# Coverage maps to docs/security-audit-v0.5.md:
#   - Archive: path traversal / absolute paths / symlink attacks
#       (meow-unit-archive-security, run below)
#   - Repository trust: unsigned-repo rejection in strict mode,
#       tampered signed metadata, invalid artifact hash
#   - Installation: concurrent install robustness, rollback cleanup
#   - Hooks: network access attempt reporting
#
# Usage:
#   test/security/run.sh            # auto-detect build/ under repo root
#   MEOW_BUILD=/path/to/build test/security/run.sh
#
# Exit code is non-zero if any check fails.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${MEOW_BUILD:-$REPO_ROOT/build}"

MEOW="$BUILD_DIR/meow"
MEOW_BUILD_BIN="$BUILD_DIR/meow-build"
MEOW_REPO_BIN="$BUILD_DIR/meow-repo"
UNIT_ARCHIVE="$BUILD_DIR/meow-unit-archive-security"
UNIT_POLICY="$BUILD_DIR/meow-unit-security-policy"
KEYS_DIR="$REPO_ROOT/test/keys"
PRIV_KEY="$KEYS_DIR/meow-release.pem"
PUB_KEY="$KEYS_DIR/meow-release.pub"

pass=0
fail=0

# Isolated scratch space. Every test gets its own HOME, repo, config, db, and
# cleans the hardcoded install root (/tmp/meow-install) so it never touches the
# real user environment or leaks between tests.
ARTIFACTS="$(mktemp -d)"
INSTALL_ROOT="/tmp/meow-install"

ok()  { echo "  PASS: $1"; pass=$((pass + 1)); }
bad() { echo "  FAIL: $1"; fail=$((fail + 1)); }

need_bin() {
    local b="$1"
    [ -x "$b" ] || { echo "SKIP: missing binary $b (build the project first)"; exit 0; }
}

# Build a minimal signed repository containing one package. Optionally embeds a
# hook script (post_install) from $4. Prints the repo directory on stdout.
make_signed_repo() {
    local name="$1" version="$2" repo="$3" hook_script="${4:-}"
    local src stage sha
    src="$(mktemp -d)"
    stage="$(mktemp -d)"
    mkdir -p "$src/files/usr/bin" "$src/scripts"
    printf '#!/bin/sh\necho hi\n' > "$src/files/usr/bin/$name"
    chmod +x "$src/files/usr/bin/$name"
    cat > "$src/package.toml" <<EOF
name = "$name"
version = "$version"
architecture = "AMD64"
description = "security fixture"
EOF
    if [ -n "$hook_script" ]; then
        cp "$hook_script" "$src/scripts/post_install"
        chmod +x "$src/scripts/post_install"
    fi
    "$MEOW_BUILD_BIN" --output "$ARTIFACTS" "$src" >/dev/null 2>&1
    sha="$(sha256sum "$ARTIFACTS/$name-$version.pkg.tar.zst" | cut -d' ' -f1)"

    mkdir -p "$repo/by-name/${name:0:2}/$name/versions"
    cat > "$repo/repository.toml" <<EOF
format_version = 1
name = "$name-repo"
repository_id = "$name-repo"
EOF
    cat > "$repo/by-name/${name:0:2}/$name/package.toml" <<EOF
format_version = 1
description = "$name"
EOF
    cat > "$repo/by-name/${name:0:2}/$name/versions/$version.toml" <<EOF
[artifact]
filename = "$name-$version.pkg.tar.zst"
url = "file://$ARTIFACTS/$name-$version.pkg.tar.zst"
sha256 = "$sha"
EOF
    "$MEOW_REPO_BIN" sign --key "$PRIV_KEY" --key-id meow-release --repo "$repo" >/dev/null 2>&1
    echo "$repo"
}

# Write a config file pointing at $1 (repo url) with optional extra TOML lines
# ($2, one per line, appended after the repository block).
write_config() {
    local url="$1" extra="${2:-}" out="$3"
    {
        echo "[[repositories]]"
        echo "id = \"sec\""
        echo "url = \"$url\""
        echo "priority = 100"
        [ -n "$extra" ] && printf '%s\n' "$extra"
    } > "$out"
}

# Run meow with an isolated HOME + the given args; stdout+stderr merged.
run_meow() {
    HOME="$TEST_HOME" "$MEOW" "$@" 2>&1
}

# ---------------------------------------------------------------------------
# Test cases
# ---------------------------------------------------------------------------

t_archive_unit() {
    echo "=== Archive extraction (C++ unit regression) ==="
    need_bin "$UNIT_ARCHIVE"
    if "$UNIT_ARCHIVE" >/dev/null 2>&1; then
        ok "archive traversal / absolute / symlink attacks rejected"
    else
        bad "archive-security unit test failed"
    fi
}

t_policy_unit() {
    echo "=== Repository security policy (C++ unit regression) ==="
    need_bin "$UNIT_POLICY"
    if "$UNIT_POLICY" >/dev/null 2>&1; then
        ok "unsigned repo rejected only when signature required"
    else
        bad "security-policy unit test failed"
    fi
}

t_unsigned_strict() {
    echo "=== Repository trust: unsigned repo rejected in strict mode ==="
    local repo db cfg
    repo="$(mktemp -d)"
    cat > "$repo/repository.toml" <<'EOF'
format_version = 1
name = "unsigned"
repository_id = "unsigned"
EOF
    mkdir -p "$repo/by-name/he/hello/versions"
    cat > "$repo/by-name/he/hello/package.toml" <<'EOF'
format_version = 1
description = "hello"
EOF
    cat > "$repo/by-name/he/hello/versions/1.0.0.toml" <<'EOF'
[artifact]
filename = "hello-1.0.0.pkg.tar.zst"
url = "file:///tmp/hello-1.0.0.pkg.tar.zst"
sha256 = "deadbeef"
EOF

    # Default policy: unsigned repo still loads (warn only).
    db="/tmp/sec-u-default.db"; rm -f "$db"
    cfg="$(mktemp -d)/cfg.toml"
    write_config "file://$repo" "" "$cfg"
    if run_meow --db-path "$db" --config "$cfg" list 2>&1 | grep -q "hello"; then
        ok "unsigned repo loads under default policy"
    else
        bad "unsigned repo should load under default policy"
    fi

    # Strict policy: unsigned repo rejected with InvalidSignature.
    db="/tmp/sec-u-strict.db"; rm -f "$db"
    cfg="$(mktemp -d)/cfg.toml"
    write_config "file://$repo" "$(printf '[security]\nrequire_repository_signature = true')" "$cfg"
    if run_meow --db-path "$db" --config "$cfg" list 2>&1 | grep -qi "InvalidSignature"; then
        ok "unsigned repo rejected (InvalidSignature) under require_repository_signature"
    else
        bad "unsigned repo must be rejected in strict mode"
    fi
}

t_tampered_metadata() {
    echo "=== Repository trust: tampered signed metadata rejected ==="
    local repo db cfg
    repo="$(mktemp -d)"
    make_signed_repo tpkg 1.0.0 "$repo" >/dev/null
    # Tamper with the signed index after signing.
    echo "# tampered" >> "$repo/repository.toml"
    db="/tmp/sec-tamper.db"; rm -f "$db"
    cfg="$(mktemp -d)/cfg.toml"
    write_config "file://$repo" "" "$cfg"
    if run_meow --db-path "$db" --config "$cfg" list 2>&1 | grep -qi "InvalidSignature"; then
        ok "tampered repository.toml detected (InvalidSignature)"
    else
        bad "tampered signed metadata must be rejected"
    fi
}

t_invalid_hash() {
    echo "=== Repository trust: invalid artifact hash rejected ==="
    local repo db cfg
    repo="$(mktemp -d)"
    make_signed_repo ihpkg 1.0.0 "$repo" >/dev/null
    # Replace the declared sha256 with a wrong value, then re-sign so the
    # (unsigned) version manifest is internally consistent but wrong.
    local vm="$repo/by-name/ih/ihpkg/versions/1.0.0.toml"
    sed -i 's/sha256 = "[0-9a-f]*"/sha256 = "0000000000000000000000000000000000000000000000000000000000000000"/' "$vm"
    "$MEOW_REPO_BIN" sign --key "$PRIV_KEY" --key-id meow-release --repo "$repo" >/dev/null 2>&1
    db="/tmp/sec-hash.db"; rm -f "$db"
    cfg="$(mktemp -d)/cfg.toml"
    write_config "file://$repo" "" "$cfg"
    local out
    out="$(run_meow --db-path "$db" --config "$cfg" install ihpkg 2>&1)"
    if echo "$out" | grep -qi "ChecksumMismatch"; then
        ok "wrong artifact sha256 rejected (ChecksumMismatch)"
    else
        bad "invalid artifact hash must be rejected"
        echo "    got: $(echo "$out" | tr '\n' '|')"
    fi
}

t_rollback_cleanup() {
    echo "=== Installation: rollback removes extracted files on failure ==="
    local repo db cfg hook
    repo="$(mktemp -d)"
    hook="$(mktemp -d)/post_install"
    cat > "$hook" <<'EOF'
#!/bin/sh
exit 3
EOF
    make_signed_repo rpkg 1.0.0 "$repo" "$hook" >/dev/null
    rm -rf "$INSTALL_ROOT"
    db="/tmp/sec-rollback.db"; rm -f "$db"
    cfg="$(mktemp -d)/cfg.toml"
    write_config "file://$repo" "" "$cfg"
    run_meow --db-path "$db" --config "$cfg" install rpkg >/dev/null 2>&1
    if run_meow --db-path "$db" --config "$cfg" installed 2>&1 | grep -qi "no packages"; then
        ok "failed install did not register the package"
    else
        bad "rollback must leave the package unregistered"
    fi
    if [ -e "$INSTALL_ROOT/usr/bin/rpkg" ]; then
        bad "rollback left extracted file $INSTALL_ROOT/usr/bin/rpkg"
    else
        ok "rollback removed extracted files"
    fi
    rm -rf "$INSTALL_ROOT"
}

t_concurrent_install() {
    echo "=== Installation: concurrent install does not hang or corrupt DB ==="
    local repo db cfg
    repo="$(mktemp -d)"
    make_signed_repo cpkg 1.0.0 "$repo" >/dev/null
    rm -rf "$INSTALL_ROOT"
    db="/tmp/sec-concurrent.db"; rm -f "$db"
    cfg="$(mktemp -d)/cfg.toml"
    write_config "file://$repo" "" "$cfg"
    # Fire two identical installs concurrently.
    run_meow --db-path "$db" --config "$cfg" install cpkg >/dev/null 2>&1 &
    p1=$!
    run_meow --db-path "$db" --config "$cfg" install cpkg >/dev/null 2>&1 &
    p2=$!
    local rc1=0 rc2=0
    wait "$p1" 2>/dev/null || rc1=$?
    wait "$p2" 2>/dev/null || rc2=$?
    if [ "$rc1" -eq 127 ] || [ "$rc2" -eq 127 ]; then
        bad "a concurrent install process failed to start"
        rm -rf "$INSTALL_ROOT"
        return
    fi
    # DB must still be queryable afterwards (no corruption / lock wedging).
    if run_meow --db-path "$db" --config "$cfg" installed >/dev/null 2>&1; then
        ok "database remains queryable after concurrent install"
    else
        bad "database corrupted/unqueryable after concurrent install"
    fi
    rm -rf "$INSTALL_ROOT"
}

t_hook_network_reporting() {
    echo "=== Hooks: network access attempt is reported ==="
    local repo db cfg hook
    repo="$(mktemp -d)"
    hook="$(mktemp -d)/post_install"
    # Tolerant network egress attempt so the hook itself succeeds; we only
    # assert that meow reports the missing network isolation.
    cat > "$hook" <<'EOF'
#!/bin/sh
(exec 3<>/dev/tcp/127.0.0.1/9) 2>/dev/null || true
echo "hook ran"
EOF
    make_signed_repo npkg 1.0.0 "$repo" "$hook" >/dev/null
    rm -rf "$INSTALL_ROOT"
    db="/tmp/sec-net.db"; rm -f "$db"
    cfg="$(mktemp -d)/cfg.toml"
    write_config "file://$repo" "" "$cfg"
    local out
    out="$(run_meow --db-path "$db" --config "$cfg" install npkg 2>&1)"
    if echo "$out" | grep -qi "network isolation unavailable"; then
        ok "hook network egress attempt is reported (advisory warning)"
    else
        bad "hook network attempt must be reported"
    fi
    if echo "$out" | grep -qi "done"; then
        ok "install completes despite advisory network warning"
    else
        bad "install should complete (network isolation is advisory only)"
    fi
    rm -rf "$INSTALL_ROOT"
}

# ---------------------------------------------------------------------------
# Harness
# ---------------------------------------------------------------------------

main() {
    need_bin "$MEOW"; need_bin "$MEOW_BUILD_BIN"; need_bin "$MEOW_REPO_BIN"

    # One isolated HOME for the whole run (trusted key installed).
    export TEST_HOME="$(mktemp -d)"
    mkdir -p "$TEST_HOME/.config/meow/keys"
    cp "$PUB_KEY" "$TEST_HOME/.config/meow/keys/meow-release.pem" 2>/dev/null

    t_archive_unit
    t_policy_unit
    t_unsigned_strict
    t_tampered_metadata
    t_invalid_hash
    t_rollback_cleanup
    t_concurrent_install
    t_hook_network_reporting

    echo ""
    echo "Security regression suite: $pass passed, $fail failed"
    [ "$fail" -eq 0 ]
}

main "$@"
