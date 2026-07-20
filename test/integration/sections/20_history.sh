#!/usr/bin/env bash
# Package history (section 36)
set -euo pipefail

run_section() {

echo "=== 36. Package history ==="
cat > meow-hist.toml << EOF
[[repositories]]
id = "main"
url = "./repo"
priority = 100

[[groups]]
name = "base-devel"
packages = ["hello", "libfoo"]
EOF

HDB="/tmp/meow-hist-$$.db"

if $MEOW --db-path "$HDB" --config meow-hist.toml install hello >/dev/null 2>&1; then
    if OUT=$($MEOW --db-path "$HDB" why hello 2>/dev/null); echo "$OUT" | grep -qi "reason: explicit"; then
        echo "  PASS: explicit install records reason"
        pass=$((pass + 1))
    else
        echo "  FAIL: explicit reason not recorded"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: explicit install failed"
    fail=$((fail + 1))
fi

if $MEOW --db-path "$HDB" --config meow-hist.toml group install base-devel >/dev/null 2>&1; then
    if OUT=$($MEOW --db-path "$HDB" why libfoo 2>/dev/null); echo "$OUT" | grep -qi "reason: group"; then
        echo "  PASS: group install records group member"
        pass=$((pass + 1))
    else
        echo "  FAIL: group member reason not recorded"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: group install failed"
    fail=$((fail + 1))
fi

if $MEOW --db-path "$HDB" --config meow-hist.toml group install base-devel >/dev/null 2>&1; then
    if OUT=$($MEOW --db-path "$HDB" why hello 2>/dev/null); echo "$OUT" | grep -qi "reason: explicit"; then
        echo "  PASS: explicit reason survives later group install"
        pass=$((pass + 1))
    else
        echo "  FAIL: explicit reason downgraded by group install"
        fail=$((fail + 1))
    fi
fi

HDB2="/tmp/meow-hist2-$$.db"
if $MEOW --db-path "$HDB2" --config meow-hist.toml install app >/dev/null 2>&1; then
    OUT=$($MEOW --db-path "$HDB2" installed 2>/dev/null)
    if echo "$OUT" | grep -q "^app"; then
        if WOUT=$($MEOW --db-path "$HDB2" why libfoo 2>/dev/null); echo "$WOUT" | grep -qi "reason: dependency"; then
            echo "  PASS: dependency install records dependency"
            pass=$((pass + 1))
        else
            echo "  FAIL: dependency reason not recorded"
            fail=$((fail + 1))
        fi
    else
        echo "  FAIL: app install did not resolve"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: dependency install failed"
    fail=$((fail + 1))
fi

if OUT=$($MEOW --db-path "$HDB" history 2>/dev/null); echo "$OUT" | grep -qi "install hello"; then
    echo "  PASS: history lists install actions"
    pass=$((pass + 1))
else
    echo "  FAIL: history missing install actions"
    fail=$((fail + 1))
fi

if OUT=$($MEOW --db-path "$HDB" history hello 2>/dev/null); echo "$OUT" | grep -q "hello" && ! echo "$OUT" | grep -q "libfoo"; then
    echo "  PASS: history <package> restricts to package"
    pass=$((pass + 1))
else
    echo "  FAIL: history <package> not scoped"
    fail=$((fail + 1))
fi

if $MEOW --db-path "$HDB" remove libfoo >/dev/null 2>&1; then
    if OUT=$($MEOW --db-path "$HDB" history libfoo 2>/dev/null); echo "$OUT" | grep -qi "remove libfoo"; then
        echo "  PASS: remove records history"
        pass=$((pass + 1))
    else
        echo "  FAIL: remove not in history"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: remove failed"
    fail=$((fail + 1))
fi

if OUT=$($MEOW --db-path "$HDB" explicitly-installed 2>/dev/null); \
   echo "$OUT" | grep -q "^hello" && ! echo "$OUT" | grep -q "libfoo"; then
    echo "  PASS: explicitly-installed lists explicit only"
    pass=$((pass + 1))
else
    echo "  FAIL: explicitly-installed incorrect"
    fail=$((fail + 1))
fi

rm -f meow-hist.toml

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
