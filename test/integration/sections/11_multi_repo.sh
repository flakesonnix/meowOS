#!/usr/bin/env bash
# Multiple repositories (section 27)
set -euo pipefail

run_section() {
    require_tools python3 curl || return 0

echo "=== 27. Multiple repositories ==="
rm -rf repo-http
mkdir -p repo-http/by-name/he/hello/versions \
         repo-http/by-name/on/onlyinhttp/versions \
         repo-http/packages

h20=$(buildPkg hello 2.0.0 usr/bin/hello)
oi=$(buildPkg onlyinhttp 1.0.0 usr/bin/onlyinhttp)

cat > repo-http/by-name/he/hello/package.toml << EOF
format_version = 1
[metadata]
name = "hello"
version = "2.0.0"
architecture = "AMD64"
description = "HTTP fixture"
EOF
cat > repo-http/by-name/he/hello/versions/2.0.0.toml << EOF
[artifact]
filename = "hello-2.0.0.pkg.tar.zst"
url = "packages/hello-2.0.0.pkg.tar.zst"
sha256 = "$h20"
EOF
cat > repo-http/by-name/on/onlyinhttp/package.toml << EOF
format_version = 1
[metadata]
name = "onlyinhttp"
version = "1.0.0"
architecture = "AMD64"
description = "HTTP-only fixture"
EOF
cat > repo-http/by-name/on/onlyinhttp/versions/1.0.0.toml << EOF
[artifact]
filename = "onlyinhttp-1.0.0.pkg.tar.zst"
url = "packages/onlyinhttp-1.0.0.pkg.tar.zst"
sha256 = "$oi"
EOF

cat > repo-http/repository.toml << EOF
format_version = 1
name = "http-fixture"
repository_id = "http-fixture"
generated = "2024-01-01T00:00:00Z"
expires = "2099-01-01T00:00:00Z"
EOF

printf 'hello 2.0.0\nonlyinhttp 1.0.0\n' > repo-http/packages.index

cp /tmp/meow-artifacts/hello-2.0.0.pkg.tar.zst \
   /tmp/meow-artifacts/onlyinhttp-1.0.0.pkg.tar.zst \
   repo-http/packages/

./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo-http >/dev/null 2>&1 || true

FREE_PORT=$(python3 -c 'import socket
s=socket.socket()
s.bind(("",0))
print(s.getsockname()[1])
s.close()')
SERVER="$ROOT_DIR/build/meow-server"
"$SERVER" serve ./repo-http --port "$FREE_PORT" >/tmp/meow-server-multi.log 2>&1 &
SRV_PID=$!
for _ in $(seq 1 50); do
    if curl -s -o /dev/null "http://127.0.0.1:$FREE_PORT/repository.toml"; then
        break
    fi
    sleep 0.1
done
BASE="http://127.0.0.1:$FREE_PORT"

cat > meow-multi.toml << EOF
[[repositories]]
id = "stable"
url = "./repo"
priority = 100

[[repositories]]
id = "testing"
url = "$BASE"
priority = 50

[[repositories]]
id = "broken"
url = "$BASE/does-not-exist"
priority = 10
EOF

MULTI_DB="/tmp/meow-multi-$$.db"

check "load multiple repositories" "onlyinhttp" \
    $MEOW --db-path "$MULTI_DB" --config meow-multi.toml list
check "load multiple repositories (fs pkg)" "libfoo" \
    $MEOW --db-path "$MULTI_DB" --config meow-multi.toml list

check "filesystem + HTTP mixed" "1.1.0" \
    $MEOW --db-path "$MULTI_DB" --config meow-multi.toml info hello

check "repository priority respected" "hello 1.1.0" \
    $MEOW --db-path "$MULTI_DB" --config meow-multi.toml install hello
check "priority: installed fs version" "hello 1.1.0" \
    $MEOW --db-path "$MULTI_DB" installed

check "package found from secondary repo" "done" \
    $MEOW --db-path "$MULTI_DB" --config meow-multi.toml install onlyinhttp
check "secondary repo package installed" "onlyinhttp 1.0.0" \
    $MEOW --db-path "$MULTI_DB" installed

STABLE_CACHE="$HOME/.cache/meow/repos/./repo"
if [ -d "$STABLE_CACHE" ] && [ -d "$HOME/.cache/meow/repos-http" ] \
   && [ -n "$(ls -A "$HOME/.cache/meow/repos-http" 2>/dev/null)" ]; then
    echo "  PASS: cache separated by repository_id"
    pass=$((pass + 1))
else
    echo "  FAIL: cache not separated by repository_id (stable=$STABLE_CACHE)"
    fail=$((fail + 1))
fi

if $MEOW --db-path "$MULTI_DB" --config meow-multi.toml list 2>&1 | grep -q "libfoo"; then
    echo "  PASS: invalid repo does not break healthy repo"
    pass=$((pass + 1))
else
    echo "  FAIL: invalid repo broke healthy repos"
    fail=$((fail + 1))
fi

kill "$SRV_PID" 2>/dev/null || true
wait "$SRV_PID" 2>/dev/null || true

rm -f meow-multi.toml
git clean -fdq repo-http 2>/dev/null || true

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
