#!/usr/bin/env bash
# Repository mirror failover (section 34)
set -euo pipefail

run_section() {

echo "=== 34. Repository mirror failover ==="
rm -rf /tmp/fo-fixture
mkdir -p /tmp/fo-fixture
FO_FIX_PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("",0));print(s.getsockname()[1]);s.close()')
python3 "$ROOT_DIR/test/http_fixture.py" --root /tmp/fo-fixture --port "$FO_FIX_PORT" >/tmp/fo-fixture.log 2>&1 &
FO_FIX_PID=$!
for _ in $(seq 1 50); do
    if curl -s -o /dev/null "http://127.0.0.1:$FO_FIX_PORT/"; then break; fi
    sleep 0.1
done
FO_FIX="http://127.0.0.1:$FO_FIX_PORT"

makePrioRepo fo-good "fogood" "fo-good-fixture" "hello" "1.0.0"
echo "hello 1.0.0" > fo-good/packages.index
FO_GOOD_PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("",0));print(s.getsockname()[1]);s.close()')
"$ROOT_DIR/build/meow-server" serve ./fo-good --port "$FO_GOOD_PORT" >/tmp/fo-good.log 2>&1 &
FO_GOOD_PID=$!
for _ in $(seq 1 50); do
    if curl -s -o /dev/null "http://127.0.0.1:$FO_GOOD_PORT/repository.toml"; then break; fi
    sleep 0.1
done
FO_GOOD="http://127.0.0.1:$FO_GOOD_PORT"

rm -rf fo-bad
cp -r fo-good fo-bad
echo "# tampered" >> fo-bad/repository.toml
FO_BAD_PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("",0));print(s.getsockname()[1]);s.close()')
"$ROOT_DIR/build/meow-server" serve ./fo-bad --port "$FO_BAD_PORT" >/tmp/fo-bad.log 2>&1 &
FO_BAD_PID=$!
for _ in $(seq 1 50); do
    if curl -s -o /dev/null "http://127.0.0.1:$FO_BAD_PORT/repository.toml"; then break; fi
    sleep 0.1
done
FO_BAD="http://127.0.0.1:$FO_BAD_PORT"

cat > meow-fo1.toml << EOF
[[repositories]]
id = "main"
priority = 100
mirrors = ["http://127.0.0.1:1/", "$FO_GOOD"]
EOF
FO_DB_1="/tmp/meow-fo1-$$.db"
if $MEOW --db-path "$FO_DB_1" --config meow-fo1.toml info hello 2>/dev/null | grep -q "Version      1.0.0"; then
    echo "  PASS: first mirror offline falls back to valid mirror"
    pass=$((pass + 1))
else
    echo "  FAIL: offline first mirror not failed over"
    fail=$((fail + 1))
fi

cat > meow-fo2.toml << EOF
[[repositories]]
id = "main"
priority = 100
mirrors = ["$FO_FIX/slow/repository.toml", "$FO_GOOD"]
EOF
FO_DB_2="/tmp/meow-fo2-$$.db"
if $MEOW --db-path "$FO_DB_2" --config meow-fo2.toml info hello 2>/dev/null | grep -q "Version      1.0.0"; then
    echo "  PASS: first mirror timeout falls back to valid mirror"
    pass=$((pass + 1))
else
    echo "  FAIL: timed-out first mirror not failed over"
    fail=$((fail + 1))
fi

cat > meow-fo3.toml << EOF
[[repositories]]
id = "main"
priority = 100
mirrors = ["$FO_BAD", "$FO_GOOD"]
EOF
FO_DB_3="/tmp/meow-fo3-$$.db"
FO_OUT_3=$($MEOW --db-path "$FO_DB_3" --config meow-fo3.toml sync 2>&1 | sed 's/\x1b\[[0-9;]*m//g')
if echo "$FO_OUT_3" | grep -q "InvalidSignature"; then
    echo "  PASS: bad signature aborts chain without fallback"
    pass=$((pass + 1))
else
    echo "  FAIL: bad signature fell through to next mirror"
    fail=$((fail + 1))
fi
if echo "$FO_OUT_3" | grep -q "main.*Available"; then
    echo "  FAIL: trust failure incorrectly fell back to healthy mirror"
    fail=$((fail + 1))
else
    echo "  PASS: healthy mirror not used after trust failure"
    pass=$((pass + 1))
fi

cat > meow-fo4.toml << EOF
[[repositories]]
id = "main"
priority = 100
mirrors = ["$FO_FIX/does-not-exist/repository.toml", "$FO_GOOD"]
EOF
FO_DB_4="/tmp/meow-fo4-$$.db"
FO_OUT_4=$($MEOW --db-path "$FO_DB_4" --config meow-fo4.toml sync 2>&1 | sed 's/\x1b\[[0-9;]*m//g')
if echo "$FO_OUT_4" | grep -q "main.*Available"; then
    echo "  FAIL: HTTP 404 incorrectly fell back to healthy mirror"
    fail=$((fail + 1))
else
    echo "  PASS: HTTP 404 does not fall back"
    pass=$((pass + 1))
fi

cat > meow-fo5.toml << EOF
[[repositories]]
id = "main"
priority = 100
mirrors = ["http://127.0.0.1:1/", "http://127.0.0.1:2/"]
EOF
FO_DB_5="/tmp/meow-fo5-$$.db"
FO_OUT_5=$($MEOW --db-path "$FO_DB_5" --config meow-fo5.toml sync 2>&1 | sed 's/\x1b\[[0-9;]*m//g')
if echo "$FO_OUT_5" | grep -q "NetworkError"; then
    echo "  PASS: both mirrors unavailable -> NetworkError"
    pass=$((pass + 1))
else
    echo "  FAIL: both mirrors unavailable not reported NetworkError"
    fail=$((fail + 1))
fi
if echo "$FO_OUT_5" | grep -q "127.0.0.1:1" && echo "$FO_OUT_5" | grep -q "127.0.0.1:2"; then
    echo "  PASS: attempt list preserved across mirrors"
    pass=$((pass + 1))
else
    echo "  FAIL: attempt history not preserved"
    fail=$((fail + 1))
fi

kill "$FO_GOOD_PID" "$FO_BAD_PID" "$FO_FIX_PID" 2>/dev/null || true
rm -f meow-fo1.toml meow-fo2.toml meow-fo3.toml meow-fo4.toml meow-fo5.toml
git clean -fdq fo-good fo-bad 2>/dev/null || true

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
