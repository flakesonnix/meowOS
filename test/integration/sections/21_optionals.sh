#!/usr/bin/env bash
# Optional dependency installation (section 37)
set -euo pipefail

run_section() {

echo "=== 37. Optional dependency installation ==="
registerOptPkg gtk4 1.0.0 usr/bin/gtk4
registerOptPkg qt6 1.0.0 usr/bin/qt6

cat > meow-opt.toml << EOF
[[repositories]]
id = "main"
url = "./repo"
priority = 100
EOF

ODB="/tmp/meow-opt-$$.db"

if $MEOW --db-path "$ODB" --config meow-opt.toml install app >/dev/null 2>&1; then
    if OUT=$($MEOW --db-path "$ODB" installed 2>/dev/null); \
       echo "$OUT" | grep -q "^app" && ! echo "$OUT" | grep -q "gtk4" && ! echo "$OUT" | grep -q "qt6"; then
        echo "  PASS: metadata only doesn't install optional"
        pass=$((pass + 1))
    else
        echo "  FAIL: optional installed without flag"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: app install failed"
    fail=$((fail + 1))
fi

ODB2="/tmp/meow-opt2-$$.db"
if $MEOW --db-path "$ODB2" --config meow-opt.toml install app --with-optional >/dev/null 2>&1; then
    if OUT=$($MEOW --db-path "$ODB2" installed 2>/dev/null); \
       echo "$OUT" | grep -q "^gtk4" && echo "$OUT" | grep -q "^qt6"; then
        echo "  PASS: --with-optional installs all optional"
        pass=$((pass + 1))
    else
        echo "  FAIL: --with-optional did not install optionals"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: --with-optional install failed"
    fail=$((fail + 1))
fi

ODB3="/tmp/meow-opt3-$$.db"
if $MEOW --db-path "$ODB3" --config meow-opt.toml install app --optional gtk4 >/dev/null 2>&1; then
    if OUT=$($MEOW --db-path "$ODB3" installed 2>/dev/null); \
       echo "$OUT" | grep -q "^gtk4" && ! echo "$OUT" | grep -q "qt6"; then
        echo "  PASS: --optional installs selected package"
        pass=$((pass + 1))
    else
        echo "  FAIL: --optional did not install only selected"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: --optional install failed"
    fail=$((fail + 1))
fi

ODB4="/tmp/meow-opt4-$$.db"
if $MEOW --db-path "$ODB4" --config meow-opt.toml install app --optional gtk4 --optional qt6 >/dev/null 2>&1; then
    if OUT=$($MEOW --db-path "$ODB4" installed 2>/dev/null); \
       echo "$OUT" | grep -q "^gtk4" && echo "$OUT" | grep -q "^qt6"; then
        echo "  PASS: multiple --optional flags"
        pass=$((pass + 1))
    else
        echo "  FAIL: multiple --optional did not install all"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: multiple --optional install failed"
    fail=$((fail + 1))
fi

if OUT=$($MEOW --db-path "$ODB" --config meow-opt.toml install app --optional nonexistent 2>&1); \
   echo "$OUT" | grep -qi "not an optional dependency"; then
    echo "  PASS: invalid optional rejected"
    pass=$((pass + 1))
else
    echo "  FAIL: invalid optional not rejected"
    fail=$((fail + 1))
fi

if OUT=$($MEOW --db-path "$ODB2" why gtk4 2>/dev/null); echo "$OUT" | grep -qi "reason: explicit"; then
    echo "  PASS: optional recorded as Explicit"
    pass=$((pass + 1))
else
    echo "  FAIL: optional not recorded as Explicit"
    fail=$((fail + 1))
fi

if OUT=$($MEOW --db-path "$ODB2" history gtk4 2>/dev/null); echo "$OUT" | grep -qi "install gtk4"; then
    echo "  PASS: history preserved"
    pass=$((pass + 1))
else
    echo "  FAIL: history missing optional install"
    fail=$((fail + 1))
fi

rm -f meow-opt.toml

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
