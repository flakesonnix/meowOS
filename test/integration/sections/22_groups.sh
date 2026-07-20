#!/usr/bin/env bash
# Package groups
set -euo pipefail

run_section() {

echo "=== Package groups ==="
cat > meow-groups.toml << EOF
[[repositories]]
id = "main"
url = "./repo"
priority = 100

[[groups]]
name = "base-devel"
packages = ["hello", "libfoo"]

[[groups]]
name = "with-deps"
packages = ["app"]
EOF

GRP_DB="/tmp/meow-grp-$$.db"

if $MEOW --db-path "$GRP_DB" --config meow-groups.toml group list 2>/dev/null | grep -q "base-devel"; then
    echo "  PASS: parse group config"
    pass=$((pass + 1))
else
    echo "  FAIL: group config not parsed"
    fail=$((fail + 1))
fi
if $MEOW --db-path "$GRP_DB" --config meow-groups.toml group list 2>/dev/null | grep -q "hello" && \
   $MEOW --db-path "$GRP_DB" --config meow-groups.toml group list 2>/dev/null | grep -q "libfoo"; then
    echo "  PASS: list groups shows members"
    pass=$((pass + 1))
else
    echo "  FAIL: group members not listed"
    fail=$((fail + 1))
fi

if $MEOW --db-path "$GRP_DB" --config meow-groups.toml group install base-devel >/dev/null 2>&1; then
    OUT=$($MEOW --db-path "$GRP_DB" installed 2>/dev/null)
    if echo "$OUT" | grep -q "^hello" && echo "$OUT" | grep -q "^libfoo"; then
        echo "  PASS: install group installs all members"
        pass=$((pass + 1))
    else
        echo "  FAIL: group members not all installed"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: group install failed"
    fail=$((fail + 1))
fi

GRP_DB2="/tmp/meow-grp2-$$.db"
if $MEOW --db-path "$GRP_DB2" --config meow-groups.toml group install with-deps >/dev/null 2>&1; then
    OUT2=$($MEOW --db-path "$GRP_DB2" installed 2>/dev/null)
    if echo "$OUT2" | grep -q "^app" && echo "$OUT2" | grep -q "^hello" && echo "$OUT2" | grep -q "^libfoo"; then
        echo "  PASS: group install resolves dependency closure"
        pass=$((pass + 1))
    else
        echo "  FAIL: group dependency closure incomplete"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: group with-deps install failed"
    fail=$((fail + 1))
fi

GRP_DB3="/tmp/meow-grp3-$$.db"
cat > meow-groups-bad.toml << EOF
[[repositories]]
id = "main"
url = "./repo"
priority = 100

[[groups]]
name = "broken"
packages = ["hello", "no-such-package"]
EOF
$MEOW --db-path "$GRP_DB3" --config meow-groups-bad.toml group install broken >/dev/null 2>&1 || true
if $MEOW --db-path "$GRP_DB3" installed 2>/dev/null | grep -q "^hello"; then
    echo "  FAIL: partial group install left packages behind"
    fail=$((fail + 1))
else
    echo "  PASS: group install is atomic (nothing installed on failure)"
    pass=$((pass + 1))
fi

if OUT=$($MEOW --db-path "$GRP_DB" --config meow-groups.toml group install nosuchgroup 2>&1); echo "$OUT" | grep -qi "group not found"; then
    echo "  PASS: missing group rejected"
    pass=$((pass + 1))
else
    echo "  FAIL: missing group not rejected"
    fail=$((fail + 1))
fi

cat > meow-groups-dup.toml << EOF
[[repositories]]
id = "main"
url = "./repo"
priority = 100

[[groups]]
name = "dup"
packages = ["hello"]

[[groups]]
name = "dup"
packages = ["libfoo"]
EOF
if OUT=$($MEOW --db-path "$GRP_DB" --config meow-groups-dup.toml list 2>&1); echo "$OUT" | grep -qi "duplicate group"; then
    echo "  PASS: duplicate group rejected"
    pass=$((pass + 1))
else
    echo "  FAIL: duplicate group not rejected"
    fail=$((fail + 1))
fi

cat > meow-groups-rsv.toml << EOF
[[repositories]]
id = "main"
url = "./repo"
priority = 100

[[groups]]
name = "install"
packages = ["hello"]
EOF
if OUT=$($MEOW --db-path "$GRP_DB" --config meow-groups-rsv.toml list 2>&1); echo "$OUT" | grep -qi "reserved command"; then
    echo "  PASS: reserved-name group rejected"
    pass=$((pass + 1))
else
    echo "  FAIL: reserved-name group not rejected"
    fail=$((fail + 1))
fi

rm -f meow-groups.toml meow-groups-bad.toml meow-groups-dup.toml meow-groups-rsv.toml

}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    . "$(cd "$(dirname "$0")/../../.." && pwd)/test/integration/common.sh"
    mkdir -p ~/.config/meow/keys
    cp "$KEYS_DIR/meow-release.pub" ~/.config/meow/keys/meow-release.pem
    cleanup
    bootstrapArtifacts
    run_section
    echo "Results: $pass passed, $fail failed"
    [ "$fail" -eq 0 ] || exit 1
fi
