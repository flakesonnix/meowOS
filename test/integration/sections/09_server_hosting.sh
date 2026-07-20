#!/usr/bin/env bash
# meow-server static repository hosting (section 25)
set -euo pipefail

run_section() {

echo "=== 25. meow-server static repository hosting ==="
SERVER="$ROOT_DIR/build/meow-server"
FREE_PORT=$(python3 -c 'import socket
s=socket.socket()
s.bind(("",0))
print(s.getsockname()[1])
s.close()')
"$SERVER" serve ./repo --port "$FREE_PORT" >/tmp/meow-server.log 2>&1 &
SRV_PID=$!
for _ in $(seq 1 50); do
    if curl -s -o /dev/null "http://127.0.0.1:$FREE_PORT/repository.toml"; then
        break
    fi
    sleep 0.1
done
BASE="http://127.0.0.1:$FREE_PORT"

ct=$($MEOW --db-path /dev/null 2>/dev/null; curl -s -D - -o /dev/null "$BASE/repository.toml" | grep -i "content-type" | tr -d '\r')
if echo "$ct" | grep -qi "application/toml"; then
    echo "  PASS: repository.toml served with application/toml"
    pass=$((pass + 1))
else
    echo "  FAIL: repository.toml wrong content-type: $ct"
    fail=$((fail + 1))
fi

if curl -s -o /dev/null -w "%{http_code}" "$BASE/repository.toml.sig" | grep -q 200; then
    echo "  PASS: repository.toml.sig served"
    pass=$((pass + 1))
else
    echo "  FAIL: repository.toml.sig not served"
    fail=$((fail + 1))
fi

if curl -s -o /dev/null -w "%{http_code}" "$BASE/by-name/he/hello/package.toml" | grep -q 200; then
    echo "  PASS: by-name package manifest served"
    pass=$((pass + 1))
else
    echo "  FAIL: by-name package manifest missing"
    fail=$((fail + 1))
fi

mkdir -p repo/packages
cp /tmp/meow-artifacts/hello-1.1.0.pkg.tar.zst repo/packages/ 2>/dev/null || true
ART=$(curl -s "$BASE/by-name/he/hello/versions/1.1.0.toml" | grep -m1 "filename" | sed -E 's/.*= *"([^"]+)".*/\1/')
if [ -n "$ART" ] && curl -s -o /dev/null -w "%{http_code}" "$BASE/packages/$ART" | grep -q 200; then
    echo "  PASS: package artifact served from packages/ ($ART)"
    pass=$((pass + 1))
else
    echo "  FAIL: package artifact not served (art=$ART)"
    fail=$((fail + 1))
fi

if curl -s -r 0-15 -o /dev/null -w "%{http_code}" "$BASE/packages/$ART" | grep -q 206; then
    echo "  PASS: range request returns 206 Partial Content"
    pass=$((pass + 1))
else
    echo "  FAIL: range request not supported"
    fail=$((fail + 1))
fi

if curl -s -o /dev/null -w "%{http_code}" "$BASE/does-not-exist" | grep -q 404; then
    echo "  PASS: missing path returns 404"
    pass=$((pass + 1))
else
    echo "  FAIL: missing path did not return 404"
    fail=$((fail + 1))
fi

kill "$SRV_PID" 2>/dev/null || true
wait "$SRV_PID" 2>/dev/null || true

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
