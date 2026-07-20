# SAT/Legacy resolver tests + unit test runners
# Section 30, unit test runners (31/32 duplicate)
run_resolver_sections() {

echo "=== 30. Version constraint regression (real repo fixtures) ==="
REGR_DB="/tmp/meow-regr-$$.db"

if $MEOW --db-path "$REGR_DB" install myapp >/dev/null 2>&1; then
    echo "  PASS: myapp install (constraint libbar>=2.0)"
    pass=$((pass + 1))
else
    echo "  FAIL: myapp install failed"
    fail=$((fail + 1))
fi
$MEOW --db-path "$REGR_DB" remove myapp >/dev/null 2>&1 || true

if $MEOW --db-path "$REGR_DB" install myapp-exact >/dev/null 2>&1; then
    echo "  PASS: myapp-exact install (constraint libbar=1.0)"
    pass=$((pass + 1))
else
    echo "  FAIL: myapp-exact install failed"
    fail=$((fail + 1))
fi

INSTALLED=$($MEOW --db-path "$REGR_DB" installed 2>/dev/null)
if echo "$INSTALLED" | grep -q "^libbar"; then
    LB_VER=$(echo "$INSTALLED" | grep "^libbar" | awk '{print $2}')
    if [ "$MEOW_RESOLVER" = "sat" ]; then
        if [ "$LB_VER" = "1.0.0" ]; then
            echo "  PASS: SAT selected libbar 1.0.0 (constraint =1.0)"
            pass=$((pass + 1))
        else
            echo "  FAIL: SAT selected libbar $LB_VER, expected 1.0.0"
            fail=$((fail + 1))
        fi
    else
        if [ "$LB_VER" = "3.0.0" ]; then
            echo "  PASS: Legacy selected libbar 3.0.0 (ignores constraint, picks highest)"
            pass=$((pass + 1))
        else
            echo "  FAIL: Legacy selected libbar $LB_VER, expected 3.0.0"
            fail=$((fail + 1))
        fi
    fi
else
    echo "  FAIL: libbar not installed"
    fail=$((fail + 1))
fi
$MEOW --db-path "$REGR_DB" remove myapp-exact >/dev/null 2>&1 || true
$MEOW --db-path "$REGR_DB" remove libbar >/dev/null 2>&1 || true

rm -f "$REGR_DB"

echo ""
echo "=== 31. In-memory backend unit tests (disk/network-free) ==="
UNIT_BIN="$(cd "$(dirname "$0")" && pwd)/../build/meow-unit-backend"
if [ ! -x "$UNIT_BIN" ]; then
    echo "  SKIP: unit test binary not built ($UNIT_BIN)"
else
    UNIT_OUT=$("$UNIT_BIN" 2>&1)
    UNIT_FAIL=$(printf '%s\n' "$UNIT_OUT" | grep -c '^  FAIL:' || true)
    UNIT_PASS=$(printf '%s\n' "$UNIT_OUT" | grep -c '^  PASS:' || true)
    if [ "$UNIT_FAIL" -eq 0 ]; then
        echo "  PASS: $UNIT_PASS in-memory backend checks passed"
        pass=$((pass + 1))
    else
        echo "  FAIL: $UNIT_FAIL in-memory backend checks failed"
        fail=$((fail + 1))
    fi
fi

echo ""
echo "=== 31. Package history unit tests (disk/network-free) ==="
HIST_BIN="$(cd "$(dirname "$0")" && pwd)/../build/meow-unit-history"
if [ ! -x "$HIST_BIN" ]; then
    echo "  SKIP: history unit test binary not built ($HIST_BIN)"
else
    HIST_OUT=$("$HIST_BIN" 2>&1)
    HIST_FAIL=$(printf '%s\n' "$HIST_OUT" | grep -c '^  FAIL:' || true)
    HIST_PASS=$(printf '%s\n' "$HIST_OUT" | grep -c '^  PASS:' || true)
    if [ "$HIST_FAIL" -eq 0 ]; then
        echo "  PASS: $HIST_PASS package history checks passed"
        pass=$((pass + 1))
    else
        echo "  FAIL: $HIST_FAIL package history checks failed"
        fail=$((fail + 1))
    fi
fi

echo ""
echo "=== 32. Optional dependency expansion unit tests (disk/network-free) ==="
OPT_BIN="$(cd "$(dirname "$0")" && pwd)/../build/meow-unit-optional"
if [ ! -x "$OPT_BIN" ]; then
    echo "  SKIP: optional unit test binary not built ($OPT_BIN)"
else
    OPT_OUT=$("$OPT_BIN" 2>&1)
    OPT_FAIL=$(printf '%s\n' "$OPT_OUT" | grep -c '^  FAIL:' || true)
    OPT_PASS=$(printf '%s\n' "$OPT_OUT" | grep -c '^  PASS:' || true)
    if [ "$OPT_FAIL" -eq 0 ]; then
        echo "  PASS: $OPT_PASS optional expansion checks passed"
        pass=$((pass + 1))
    else
        echo "  FAIL: $OPT_FAIL optional expansion checks failed"
        fail=$((fail + 1))
    fi
fi

}
