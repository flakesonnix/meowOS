#!/usr/bin/env bash
# Repository mirror groups (section 33)
set -euo pipefail

run_section() {

echo "=== 33. Repository mirror groups ==="
makePrioRepo mir-a "mira" "mir-a-fixture" "hello" "1.0.0"
cat > meow-mir-a.toml << EOF
[[repositories]]
id = "main"
priority = 100
mirrors = ["./mir-a"]
EOF

MIR_DB_A="/tmp/meow-mir-a-$$.db"
if $MEOW --db-path "$MIR_DB_A" --config meow-mir-a.toml info hello 2>/dev/null | grep -q "Version      1.0.0"; then
    echo "  PASS: single mirror behaves like old url config"
    pass=$((pass + 1))
else
    echo "  FAIL: single-mirror source did not load"
    fail=$((fail + 1))
fi

makePrioRepo mir-b "mirb" "mir-shared-fixture" "widget" "1.0.0"
cp -r mir-a mir-a2
cat > meow-mir-b.toml << EOF
[[repositories]]
id = "src-one"
priority = 100
mirrors = ["./mir-a"]

[[repositories]]
id = "src-two"
priority = 50
mirrors = ["./mir-a2"]
EOF

MIR_DB_B="/tmp/meow-mir-b-$$.db"
MIR_B_OUT=$($MEOW --db-path "$MIR_DB_B" --config meow-mir-b.toml list 2>/dev/null)
if echo "$MIR_B_OUT" | grep -q "^hello$" && [ "$(echo "$MIR_B_OUT" | grep -cx "^hello$")" -eq 1 ]; then
    echo "  PASS: multiple mirrors share repository_id"
    pass=$((pass + 1))
else
    echo "  FAIL: same repository_id not deduped across sources"
    fail=$((fail + 1))
fi

MIR_ID="mir-cache-fixture"
makePrioRepo mir-cache "mirc" "$MIR_ID" "cached" "1.0.0"
cp -r mir-cache mir-cache2
cat > meow-mir-c.toml << EOF
[[repositories]]
id = "cached-src"
priority = 100
mirrors = ["./mir-cache", "./mir-cache2"]
EOF

MIR_DB_C="/tmp/meow-mir-c-$$.db"
$MEOW --db-path "$MIR_DB_C" --config meow-mir-c.toml sync >/dev/null 2>&1 || true
MIR_CACHE_ROOT="$HOME/.cache/meow/repos"
if [ -f "$MIR_CACHE_ROOT/$MIR_ID/repository.toml" ]; then
    echo "  PASS: cache shared between mirrors"
    pass=$((pass + 1))
else
    echo "  FAIL: cache not keyed by repository_id"
    fail=$((fail + 1))
fi
if [ ! -d "$MIR_CACHE_ROOT/mir-cache" ] && [ ! -d "$MIR_CACHE_ROOT/mir-cache2" ]; then
    echo "  PASS: cache not keyed by mirror path"
    pass=$((pass + 1))
else
    echo "  FAIL: cache keyed by mirror path"
    fail=$((fail + 1))
fi

makePrioRepo mir-prio-hi "hi" "mir-prio-hi-fixture" "hello" "1.0.0"
makePrioRepo mir-prio-lo "lo" "mir-prio-lo-fixture" "hello" "2.0.0"
cat > meow-mir-d.toml << EOF
[[repositories]]
id = "high"
priority = 100
mirrors = ["./mir-prio-hi"]

[[repositories]]
id = "low"
priority = 50
mirrors = ["./mir-prio-lo", "./mir-prio-lo"]
EOF

MIR_DB_D="/tmp/meow-mir-d-$$.db"
if $MEOW --db-path "$MIR_DB_D" --config meow-mir-d.toml info hello 2>/dev/null | grep -q "Version      1.0.0"; then
    echo "  PASS: mirror config preserves repository priority"
    pass=$((pass + 1))
else
    echo "  FAIL: priority lost with mirror config"
    fail=$((fail + 1))
fi

makePrioRepo mir-legacy "leg" "mir-legacy-fixture" "hello" "1.0.0"
cat > meow-mir-e.toml << EOF
[[repositories]]
id = "legacy"
priority = 100
url = "./mir-legacy"
EOF

MIR_DB_E="/tmp/meow-mir-e-$$.db"
if $MEOW --db-path "$MIR_DB_E" --config meow-mir-e.toml info hello 2>/dev/null | grep -q "Version      1.0.0"; then
    echo "  PASS: legacy url config migrates internally"
    pass=$((pass + 1))
else
    echo "  FAIL: legacy url config not migrated to mirror list"
    fail=$((fail + 1))
fi

rm -f meow-mir-a.toml meow-mir-b.toml meow-mir-c.toml meow-mir-d.toml meow-mir-e.toml
git clean -fdq mir-a mir-a2 mir-b mir-cache mir-cache2 mir-prio-hi mir-prio-lo mir-legacy 2>/dev/null || true

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
