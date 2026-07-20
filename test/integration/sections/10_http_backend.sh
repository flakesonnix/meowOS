#!/usr/bin/env bash
# HTTP repository backend (section 26)
set -euo pipefail

run_section() {
    require_tools python3 curl || return 0

echo "=== 26. HTTP repository backend ==="
rm -rf repo-http
mkdir -p repo-http/by-name/he/hello/versions \
         repo-http/by-name/li/libfoo/versions \
         repo-http/by-name/ap/app/versions \
         repo-http/packages

h10=$(buildPkg hello 1.0.0 usr/bin/hello)
h11=$(buildPkg hello 1.1.0 usr/bin/hello)
lf=$(buildPkg libfoo 1.0.0 usr/lib/libfoo.so 'provides = ["foo-lib"]')

cat > repo-http/by-name/he/hello/package.toml << EOF
format_version = 1
[metadata]
name = "hello"
version = "1.1.0"
architecture = "AMD64"
description = "HTTP fixture"
EOF
cat > repo-http/by-name/he/hello/versions/1.0.0.toml << EOF
[artifact]
filename = "hello-1.0.0.pkg.tar.zst"
url = "packages/hello-1.0.0.pkg.tar.zst"
sha256 = "$h10"
EOF
cat > repo-http/by-name/he/hello/versions/1.1.0.toml << EOF
[artifact]
filename = "hello-1.1.0.pkg.tar.zst"
url = "packages/hello-1.1.0.pkg.tar.zst"
sha256 = "$h11"
EOF
cat > repo-http/by-name/li/libfoo/package.toml << EOF
format_version = 1
[metadata]
name = "libfoo"
version = "1.0.0"
architecture = "AMD64"
description = "HTTP fixture"
provides = ["foo-lib"]
EOF
cat > repo-http/by-name/li/libfoo/versions/1.0.0.toml << EOF
[artifact]
filename = "libfoo-1.0.0.pkg.tar.zst"
url = "packages/libfoo-1.0.0.pkg.tar.zst"
sha256 = "$lf"
EOF

cat > repo-http/repository.toml << EOF
format_version = 1
name = "http-fixture"
repository_id = "http-fixture"
generated = "2024-01-01T00:00:00Z"
expires = "2099-01-01T00:00:00Z"
EOF

printf 'hello 1.0.0\nhello 1.1.0\nlibfoo 1.0.0\n' > repo-http/packages.index

cp /tmp/meow-artifacts/hello-1.0.0.pkg.tar.zst \
   /tmp/meow-artifacts/hello-1.1.0.pkg.tar.zst \
   /tmp/meow-artifacts/libfoo-1.0.0.pkg.tar.zst \
   repo-http/packages/

./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo-http >/dev/null 2>&1 || true

FREE_PORT=$(python3 -c 'import socket
s=socket.socket()
s.bind(("",0))
print(s.getsockname()[1])
s.close()')
SERVER="$ROOT_DIR/build/meow-server"
"$SERVER" serve ./repo-http --port "$FREE_PORT" >/tmp/meow-server-http.log 2>&1 &
SRV_PID=$!
for _ in $(seq 1 50); do
    if curl -s -o /dev/null "http://127.0.0.1:$FREE_PORT/repository.toml"; then
        break
    fi
    sleep 0.1
done
BASE="http://127.0.0.1:$FREE_PORT"

HTTP_DB="/tmp/meow-http-$$.db"

check "load repository.toml over HTTP" "hello" \
    $MEOW --db-path "$HTTP_DB" --repository "$BASE" list

check "verify HTTP repository signature" "libfoo" \
    $MEOW --db-path "$HTTP_DB" --repository "$BASE" list

rm -rf repo-http-bad
cp -r repo-http repo-http-bad
echo "# corrupted" >> repo-http-bad/repository.toml.sig
BAD_PORT=$(python3 -c 'import socket
s=socket.socket()
s.bind(("",0))
print(s.getsockname()[1])
s.close()')
"$SERVER" serve ./repo-http-bad --port "$BAD_PORT" >/tmp/meow-server-http-bad.log 2>&1 &
BAD_PID=$!
for _ in $(seq 1 50); do
    if curl -s -o /dev/null "http://127.0.0.1:$BAD_PORT/repository.toml"; then
        break
    fi
    sleep 0.1
done
BAD_BASE="http://127.0.0.1:$BAD_PORT"
if $MEOW --db-path "$HTTP_DB" --repository "$BAD_BASE" list 2>&1 | grep -qi "signature\|invalid"; then
    echo "  PASS: reject invalid signature over HTTP"
    pass=$((pass + 1))
else
    echo "  FAIL: invalid signature over HTTP was accepted"
    fail=$((fail + 1))
fi
kill "$BAD_PID" 2>/dev/null || true
wait "$BAD_PID" 2>/dev/null || true

check "load package manifest over HTTP" "1.1.0" \
    $MEOW --db-path "$HTTP_DB" --repository "$BASE" info hello

check "install package over HTTP backend" "done" \
    $MEOW --db-path "$HTTP_DB" --repository "$BASE" install hello
check "hello installed over HTTP" "hello 1.1.0" \
    $MEOW --db-path "$HTTP_DB" installed

check "verify installed package over HTTP" "all files intact" \
    $MEOW --db-path "$HTTP_DB" verify

kill "$SRV_PID" 2>/dev/null || true
wait "$SRV_PID" 2>/dev/null || true

git clean -fdq repo-http repo-http-bad 2>/dev/null || true

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
