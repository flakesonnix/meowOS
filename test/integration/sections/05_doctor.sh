#!/usr/bin/env bash
# Doctor diagnostics and security (sections 18, 21)
set -euo pipefail

run_section() {

echo "=== 18. doctor diagnostics ==="
check "doctor runs" "meow doctor:" $MEOW --db-path "$TEST_DB" doctor
check "doctor --json has checks" '"checks": [' $MEOW --db-path "$TEST_DB" doctor --json

echo ""
echo "=== 21. doctor --security ==="
sec=$(MEOW_HOOK_TIMEOUT=30 $MEOW --db-path "$TEST_DB" doctor --security 2>&1)
for needle in "trusted keys configured" "signature valid" "metadata not expired" "no stale partial downloads" "hook policy loaded"; do
    if echo "$sec" | grep -q "$needle"; then
        echo "  PASS: security check present: $needle"
        pass=$((pass + 1))
    else
        echo "  FAIL: missing security check: $needle"
        fail=$((fail + 1))
    fi
done
if [ -d "$HOME/.cache/meow" ]; then
    touch "$HOME/.cache/meow/stale-$$.pkg.tar.zst.part"
    sec2=$($MEOW --db-path "$TEST_DB" doctor --security 2>&1)
    if echo "$sec2" | grep -q "stale .part"; then
        echo "  PASS: stale partial download detected"
        pass=$((pass + 1))
    else
        echo "  FAIL: stale partial download not detected"
        fail=$((fail + 1))
    fi
    rm -f "$HOME/.cache/meow/stale-$$.pkg.tar.zst.part"
fi

}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    . "$(cd "$(dirname "$0")/../../.." && pwd)/test/integration/common.sh"
    mkdir -p ~/.config/meow/keys
    cp "$KEYS_DIR/meow-release.pub" ~/.config/meow/keys/meow-release.pem
    cleanup
    bootstrapArtifacts
    # Install app first so doctor has a populated DB to inspect
    $MEOW --db-path "$TEST_DB" install app >/dev/null 2>&1 || true
    run_section
    echo "Results: $pass passed, $fail failed"
    [ "$fail" -eq 0 ] || exit 1
fi
