#!/usr/bin/env bash
# Parallel repository refresh (section 35)
set -euo pipefail

run_section() {
    require_tools python3 curl || return 0

echo "=== 35. Parallel repository refresh ==="
cat > /tmp/fo-slow.py << 'PY'
import http.server, socketserver, time, sys, os
ROOT = sys.argv[1]
DELAY = float(sys.argv[2])
class H(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        time.sleep(DELAY)
        rel = self.path.lstrip("/")
        p = os.path.join(ROOT, rel)
        if not os.path.isfile(p):
            self.send_error(404); return
        data = open(p, "rb").read()
        self.send_response(200)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
    def log_message(self, *a): pass
socketserver.TCPServer.allow_reuse_address = True
httpd = socketserver.TCPServer(("127.0.0.1", 0), H)
print(f"LISTENING_ON={httpd.server_address[1]}", flush=True)
httpd.serve_forever()
PY

makePrioRepo fo-good "fogood" "fo-good-fixture" "hello" "1.0.0"
echo "hello 1.0.0" > fo-good/packages.index
rm -rf fo-bad
cp -r fo-good fo-bad
echo "# tampered" >> fo-bad/repository.toml
FO_GOOD_PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("",0));print(s.getsockname()[1]);s.close()')
"$ROOT_DIR/build/meow-server" serve ./fo-good --port "$FO_GOOD_PORT" >/tmp/fo-good.log 2>&1 &
FO_GOOD_PID=$!
for _ in $(seq 1 50); do curl -s -o /dev/null "http://127.0.0.1:$FO_GOOD_PORT/repository.toml" && break; sleep 0.1; done
FO_GOOD="http://127.0.0.1:$FO_GOOD_PORT"

rm -rf fo-slowroot
mkdir -p fo-slowroot/a fo-slowroot/b
cp -r fo-good/. fo-slowroot/a/
cp -r fo-good/. fo-slowroot/b/
FO_SLOW_PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("",0));print(s.getsockname()[1]);s.close()')
python3 /tmp/fo-slow.py "$(pwd)/fo-slowroot" 1.2 >/tmp/fo-slow.log 2>&1 &
FO_SLOW_PID=$!
for _ in $(seq 1 50); do curl -s -o /dev/null "http://127.0.0.1:$FO_SLOW_PORT/a/repository.toml" && break; sleep 0.1; done
FO_SLOW="http://127.0.0.1:$FO_SLOW_PORT"
cat > meow-pa1.toml << EOF
[[repositories]]
id = "a"
priority = 100
mirrors = ["$FO_SLOW/a"]

[[repositories]]
id = "b"
priority = 100
mirrors = ["$FO_SLOW/b"]
EOF
PA_DB="/tmp/meow-pa1-$$.db"
START=$(date +%s%3N)
$MEOW --db-path "$PA_DB" --config meow-pa1.toml sync >/dev/null 2>&1 || true
END=$(date +%s%3N)
ELAPSED=$((END - START))
if [ "$ELAPSED" -lt 2200 ]; then
    echo "  PASS: parallel refresh faster than serial (${ELAPSED}ms < 2200ms)"
    pass=$((pass + 1))
else
    echo "  FAIL: refresh not parallel (${ELAPSED}ms)"
    fail=$((fail + 1))
fi

FO_BAD_PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("",0));print(s.getsockname()[1]);s.close()')
"$ROOT_DIR/build/meow-server" serve ./fo-bad --port "$FO_BAD_PORT" >/tmp/fo-bad.log 2>&1 &
FO_BAD_PID=$!
for _ in $(seq 1 50); do curl -s -o /dev/null "http://127.0.0.1:$FO_BAD_PORT/repository.toml" && break; sleep 0.1; done
FO_BAD="http://127.0.0.1:$FO_BAD_PORT"
cat > meow-pa2.toml << EOF
[[repositories]]
id = "good"
priority = 100
mirrors = ["$FO_GOOD"]

[[repositories]]
id = "bad"
priority = 50
mirrors = ["$FO_BAD"]
EOF
PA_DB_2="/tmp/meow-pa2-$$.db"
PA_OUT_2=$($MEOW --db-path "$PA_DB_2" --config meow-pa2.toml sync 2>&1 | sed 's/\x1b\[[0-9;]*m//g')
if echo "$PA_OUT_2" | grep -q "good.*Available"; then
    echo "  PASS: healthy repository available during parallel refresh"
    pass=$((pass + 1))
else
    echo "  FAIL: healthy repo blocked by broken one"
    fail=$((fail + 1))
fi
if echo "$PA_OUT_2" | grep -q "bad.*InvalidSignature"; then
    echo "  PASS: broken repository reported without blocking others"
    pass=$((pass + 1))
else
    echo "  FAIL: broken repo not isolated in parallel refresh"
    fail=$((fail + 1))
fi

cat > meow-pa3.toml << EOF
[[repositories]]
id = "main"
priority = 100
mirrors = ["http://127.0.0.1:1/", "$FO_GOOD"]
EOF
PA_DB_3="/tmp/meow-pa3-$$.db"
if $MEOW --db-path "$PA_DB_3" --config meow-pa3.toml info hello 2>/dev/null | grep -q "Version      1.0.0"; then
    echo "  PASS: mirror failover respected during parallel refresh"
    pass=$((pass + 1))
else
    echo "  FAIL: failover bypassed by parallel refresh"
    fail=$((fail + 1))
fi

makePrioRepo fo-foo "foo" "foo-id" "hello" "1.0.0"
makePrioRepo fo-bar "bar" "bar-id" "hello" "1.0.0"
FO_FOO_PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("",0));print(s.getsockname()[1]);s.close()')
FO_BAR_PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("",0));print(s.getsockname()[1]);s.close()')
"$ROOT_DIR/build/meow-server" serve ./fo-foo --port "$FO_FOO_PORT" >/tmp/fo-foo.log 2>&1 &
FO_FOO_PID=$!
"$ROOT_DIR/build/meow-server" serve ./fo-bar --port "$FO_BAR_PORT" >/tmp/fo-bar.log 2>&1 &
FO_BAR_PID=$!
for _ in $(seq 1 50); do
    curl -s -o /dev/null "http://127.0.0.1:$FO_FOO_PORT/repository.toml" && \
    curl -s -o /dev/null "http://127.0.0.1:$FO_BAR_PORT/repository.toml" && break
    sleep 0.1
done
cat > meow-pa4.toml << EOF
[[repositories]]
id = "foo"
priority = 100
mirrors = ["http://127.0.0.1:$FO_FOO_PORT/"]

[[repositories]]
id = "bar"
priority = 50
mirrors = ["http://127.0.0.1:$FO_BAR_PORT/"]
EOF
PA_DB_4="/tmp/meow-pa4-$$.db"
$MEOW --db-path "$PA_DB_4" --config meow-pa4.toml sync >/dev/null 2>&1 || true
FO_CACHE_ROOT="$HOME/.cache/meow/repos"
if [ -d "$FO_CACHE_ROOT/foo-id" ] && [ -d "$FO_CACHE_ROOT/bar-id" ] \
   && [ "$FO_CACHE_ROOT/foo-id" != "$FO_CACHE_ROOT/bar-id" ]; then
    echo "  PASS: distinct repository_id keep separate cache dirs"
    pass=$((pass + 1))
else
    echo "  FAIL: cache not isolated by repository_id"
    fail=$((fail + 1))
fi

makePrioRepo pa-hi "hi" "pa-hi-fixture" "hello" "1.0.0"
makePrioRepo pa-lo "lo" "pa-lo-fixture" "hello" "2.0.0"
cat > meow-pa5.toml << EOF
[[repositories]]
id = "high"
priority = 100
mirrors = ["./pa-hi"]

[[repositories]]
id = "low"
priority = 50
mirrors = ["./pa-lo"]
EOF
PA_DB_5="/tmp/meow-pa5-$$.db"
V1=$($MEOW --db-path "$PA_DB_5" --config meow-pa5.toml info hello 2>/dev/null | grep "Version" | awk '{print $2}')
V2=$($MEOW --db-path "$PA_DB_5" --config meow-pa5.toml info hello 2>/dev/null | grep "Version" | awk '{print $2}')
if [ "$V1" = "1.0.0" ] && [ "$V1" = "$V2" ]; then
    echo "  PASS: selection deterministic across parallel refreshes ($V1)"
    pass=$((pass + 1))
else
    echo "  FAIL: selection nondeterministic ($V1 vs $V2)"
    fail=$((fail + 1))
fi

kill "$FO_GOOD_PID" "$FO_SLOW_PID" "$FO_FOO_PID" "$FO_BAR_PID" "$FO_BAD_PID" 2>/dev/null || true
rm -f meow-pa1.toml meow-pa2.toml meow-pa3.toml meow-pa4.toml meow-pa5.toml /tmp/fo-slow.py
git clean -fdq fo-good fo-bad fo-foo fo-bar fo-slowroot pa-hi pa-lo 2>/dev/null || true

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
