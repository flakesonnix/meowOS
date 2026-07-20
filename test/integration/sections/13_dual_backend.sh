#!/usr/bin/env bash
# Dual-backend parity: filesystem vs HTTP (section 29)
set -euo pipefail

run_section() {

echo "=== 29. Dual-backend parity (filesystem vs HTTP) ==="
rm -rf repo-dual
cp -r repo repo-dual
: > repo-dual/packages.index
for shard in repo-dual/by-name/*/; do
    for pkg in "$shard"*/; do
        name=$(basename "$pkg")
        for v in "$pkg"versions/*.toml; do
            [ -e "$v" ] || continue
            ver=$(basename "$v" .toml)
            echo "$name $ver" >> repo-dual/packages.index
        done
    done
done
./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo-dual >/dev/null 2>&1 || true

FREE_PORT=$(python3 -c 'import socket
s=socket.socket()
s.bind(("",0))
print(s.getsockname()[1])
s.close()')
SERVER="$ROOT_DIR/build/meow-server"
"$SERVER" serve ./repo-dual --port "$FREE_PORT" >/tmp/meow-server-dual.log 2>&1 &
SRV_PID=$!
for _ in $(seq 1 50); do
    if curl -s -o /dev/null "http://127.0.0.1:$FREE_PORT/repository.toml"; then
        break
    fi
    sleep 0.1
done
HTTP_BASE="http://127.0.0.1:$FREE_PORT"

run_scenario() {
    local prefix="$1"
    local db="/tmp/meow-dual-$$.db"
    rm -f "$db"
    $MEOW --db-path "$db" $prefix install hello >/dev/null 2>&1 || true
    $MEOW --db-path "$db" installed 2>/dev/null | sort
    $MEOW --db-path "$db" $prefix remove hello >/dev/null 2>&1 || true
}

FS_STATE=$(run_scenario "")
HTTP_STATE=$(run_scenario "--repository $HTTP_BASE")

if [ "$FS_STATE" = "$HTTP_STATE" ]; then
    echo "  PASS: filesystem and HTTP backends produce identical results"
    pass=$((pass + 1))
else
    echo "  FAIL: backend results differ"
    echo "    fs:    $(echo "$FS_STATE" | tr '\n' '|')"
    echo "    http:  $(echo "$HTTP_STATE" | tr '\n' '|')"
    fail=$((fail + 1))
fi

DUAL_DB="/tmp/meow-dual-list-$$.db"
FS_LIST=$($MEOW --db-path "$DUAL_DB" list 2>/dev/null | grep -v '^\[' | sort)
HTTP_LIST=$($MEOW --db-path "$DUAL_DB" --repository "$HTTP_BASE" list 2>/dev/null | grep -v '^\[' | sort)
if [ "$FS_LIST" = "$HTTP_LIST" ]; then
    echo "  PASS: list identical across backends"
    pass=$((pass + 1))
else
    echo "  FAIL: list differs across backends"
    fail=$((fail + 1))
fi

FS_INFO=$($MEOW --db-path "$DUAL_DB" info hello 2>/dev/null | grep -i version | head -1)
HTTP_INFO=$($MEOW --db-path "$DUAL_DB" --repository "$HTTP_BASE" info hello 2>/dev/null | grep -i version | head -1)
if [ "$FS_INFO" = "$HTTP_INFO" ]; then
    echo "  PASS: info identical across backends"
    pass=$((pass + 1))
else
    echo "  FAIL: info differs across backends (fs='$FS_INFO' http='$HTTP_INFO')"
    fail=$((fail + 1))
fi

if OUT=$($MEOW --db-path "$DUAL_DB" info app 2>/dev/null); \
   echo "$OUT" | grep -qi "Optional dependencies" && \
   echo "$OUT" | grep -q "gtk4" && echo "$OUT" | grep -q "qt6"; then
    echo "  PASS: info shows optional dependencies"
    pass=$((pass + 1))
else
    echo "  FAIL: info missing optional dependencies"
    fail=$((fail + 1))
fi

kill "$SRV_PID" 2>/dev/null || true
wait "$SRV_PID" 2>/dev/null || true
git clean -fdq repo-dual 2>/dev/null || true

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
