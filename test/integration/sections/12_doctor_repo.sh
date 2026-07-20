#!/usr/bin/env bash
# Doctor reports per-repository status (section 28)
set -euo pipefail

run_section() {
    require_tools python3 curl || return 0

echo "=== 28. Doctor reports per-repository status ==="
rm -rf repo-http
mkdir -p repo-http/by-name/he/hello/versions repo-http/packages
h20=$(buildPkg hello 2.0.0 usr/bin/hello)
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
cat > repo-http/repository.toml << EOF
format_version = 1
name = "http-fixture"
repository_id = "http-fixture"
generated = "2024-01-01T00:00:00Z"
expires = "2099-01-01T00:00:00Z"
EOF
printf 'hello 2.0.0\n' > repo-http/packages.index
cp /tmp/meow-artifacts/hello-2.0.0.pkg.tar.zst repo-http/packages/
./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo-http >/dev/null 2>&1 || true

FREE_PORT=$(python3 -c 'import socket
s=socket.socket()
s.bind(("",0))
print(s.getsockname()[1])
s.close()')
SERVER="$ROOT_DIR/build/meow-server"
"$SERVER" serve ./repo-http --port "$FREE_PORT" >/tmp/meow-server-doc.log 2>&1 &
SRV_PID=$!
for _ in $(seq 1 50); do
    if curl -s -o /dev/null "http://127.0.0.1:$FREE_PORT/repository.toml"; then
        break
    fi
    sleep 0.1
done
BASE="http://127.0.0.1:$FREE_PORT"

cat > meow-doc.toml << EOF
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

DOC_DB="/tmp/meow-doc-$$.db"
DOC_OUT=$($MEOW --db-path "$DOC_DB" --config meow-doc.toml doctor --json 2>&1 || true)

if echo "$DOC_OUT" | grep -q '\[stable\]' && echo "$DOC_OUT" | grep -q '\[testing\]'; then
    echo "  PASS: doctor reports each configured repository"
    pass=$((pass + 1))
else
    echo "  FAIL: doctor did not report per-repository status"
    fail=$((fail + 1))
fi

if echo "$DOC_OUT" | grep -q '\[broken\]'; then
    echo "  PASS: doctor reports failed repository"
    pass=$((pass + 1))
else
    echo "  FAIL: doctor did not report the failed repository"
    fail=$((fail + 1))
fi

if ! $MEOW --db-path "$DOC_DB" --config meow-doc.toml doctor >/dev/null 2>&1; then
    echo "  PASS: doctor exits non-zero with a broken repository"
    pass=$((pass + 1))
else
    echo "  FAIL: doctor reported healthy despite a broken repository"
    fail=$((fail + 1))
fi

kill "$SRV_PID" 2>/dev/null || true
wait "$SRV_PID" 2>/dev/null || true
rm -f meow-doc.toml
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
