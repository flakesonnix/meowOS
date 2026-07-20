# Repository metadata, HTTP backends, mirrors, priority (sections 11-14, 25-29, 31-35)
run_repository_sections() {

echo "=== 11. Format version rejection ==="
# 11a.
cp repo/by-name/he/hello/package.toml /tmp/hello-pkg-toml-bak
cat > repo/by-name/he/hello/package.toml << 'EOF'
format_version = 99

[metadata]
name = "hello"
version = "1.1.0"
architecture = "AMD64"
description = "bad"
EOF
check "reject bad repo metadata format" "InvalidMetadata" $MEOW --db-path "$TEST_DB" list
cp /tmp/hello-pkg-toml-bak repo/by-name/he/hello/package.toml
rm -f /tmp/hello-pkg-toml-bak

# 11b.
cp repo/repository.toml /tmp/repo-toml-bak
cat > repo/repository.toml << 'EOF'
format_version = 99
name = "local"
EOF
check "reject bad repository format" "InvalidMetadata" $MEOW --db-path "$TEST_DB" list
cp /tmp/repo-toml-bak repo/repository.toml
rm -f /tmp/repo-toml-bak

# 11c.
cat > /tmp/meow.lock << 'EOF'
lockfile_version = 99
EOF
ln -sf "$PWD/repo" /tmp/repo
OLDPWD=$PWD
cd /tmp
check "reject bad lockfile format" "unsupported lockfile format" $MEOW --db-path /tmp/lock-test.db install --locked hello
cd "$OLDPWD"
rm -f /tmp/meow.lock /tmp/lock-test.db /tmp/repo

# 11d.
rm -f /tmp/bad-schema.db
python3 - <<'PY'
import sqlite3
c = sqlite3.connect("/tmp/bad-schema.db")
c.executescript("""
CREATE TABLE IF NOT EXISTS metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL);
INSERT INTO metadata (key, value) VALUES ('schema_version', '99');
""")
c.commit(); c.close()
PY
check "reject bad database schema" "unsupported database schema version" $MEOW --db-path /tmp/bad-schema.db list
rm -f /tmp/bad-schema.db

echo "=== 12. Key trust ==="
check "keys list shows meow-release" "meow-release" $MEOW keys list

cp "$(dirname "$0")/keys/meow-release.pub" /tmp/test-add-key.pub
check "keys add works" "added key" $MEOW keys add /tmp/test-add-key.pub
rm -f /tmp/test-add-key.pub /tmp/meow-release.pub

mv ~/.config/meow/keys/meow-release.pem /tmp/meow-key-bak
check "reject missing trusted key" "InvalidSignature" $MEOW --db-path "$TEST_DB" list
mv /tmp/meow-key-bak ~/.config/meow/keys/meow-release.pem

cp repo/repository.toml /tmp/repo-toml-bak2
echo "# tamper" >> repo/repository.toml
check "reject bad repository signature" "InvalidSignature" $MEOW --db-path "$TEST_DB" list
cp /tmp/repo-toml-bak2 repo/repository.toml
rm -f /tmp/repo-toml-bak2

echo "=== 13. Repository metadata expiry ==="
check "accept valid repository" "app" $MEOW --db-path "$TEST_DB" list

cp repo/repository.toml /tmp/repo-toml-bak3
cp repo/repository.toml.sig /tmp/repo-sig-bak3
yesterday=$(date -u -d 'yesterday' +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date -u -v-1d +%Y-%m-%dT%H:%M:%SZ)
sed -i "s/expires = \".*\"/expires = \"$yesterday\"/" repo/repository.toml
check "signature failure precedes expiry" "InvalidSignature" $MEOW --db-path "$TEST_DB" list
cp /tmp/repo-toml-bak3 repo/repository.toml
cp /tmp/repo-sig-bak3 repo/repository.toml.sig
rm -f /tmp/repo-toml-bak3 /tmp/repo-sig-bak3

cp repo/repository.toml /tmp/repo-toml-bak4
cp repo/repository.toml.sig /tmp/repo-sig-bak4
yesterday=$(date -u -d 'yesterday' +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date -u -v-1d +%Y-%m-%dT%H:%M:%SZ)
sed -i "s/expires = \".*\"/expires = \"$yesterday\"/" repo/repository.toml
./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
check "reject expired repository" "Expired" $MEOW --db-path "$TEST_DB" list
cp /tmp/repo-toml-bak4 repo/repository.toml
cp /tmp/repo-sig-bak4 repo/repository.toml.sig
rm -f /tmp/repo-toml-bak4 /tmp/repo-sig-bak4

echo "=== 14. Repository identity ==="
check "accept repository_id" "app" $MEOW --db-path "$TEST_DB" list

cp repo/repository.toml /tmp/repo-toml-bak5
cp repo/repository.toml.sig /tmp/repo-sig-bak5
sed -i '/^repository_id = /d' repo/repository.toml
./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
check "reject missing repository_id" "InvalidMetadata" $MEOW --db-path "$TEST_DB" list
cp /tmp/repo-toml-bak5 repo/repository.toml
cp /tmp/repo-sig-bak5 repo/repository.toml.sig
rm -f /tmp/repo-toml-bak5 /tmp/repo-sig-bak5

cp repo/repository.toml /tmp/repo-toml-bak6
cp repo/repository.toml.sig /tmp/repo-sig-bak6
sed -i 's/^repository_id = .*/repository_id = "bad id!"/' repo/repository.toml
./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
check "reject invalid repository_id" "InvalidMetadata" $MEOW --db-path "$TEST_DB" list
cp /tmp/repo-toml-bak6 repo/repository.toml
cp /tmp/repo-sig-bak6 repo/repository.toml.sig
rm -f /tmp/repo-toml-bak6 /tmp/repo-sig-bak6

echo ""
echo "=== 25. meow-server static repository hosting ==="
SERVER="$(cd "$(dirname "$0")" && pwd)/../build/meow-server"
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

echo ""
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

./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo-http >/dev/null 2>&1 || true

FREE_PORT=$(python3 -c 'import socket
s=socket.socket()
s.bind(("",0))
print(s.getsockname()[1])
s.close()')
SERVER="$(cd "$(dirname "$0")" && pwd)/../build/meow-server"
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

echo ""
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

./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo-http >/dev/null 2>&1 || true

FREE_PORT=$(python3 -c 'import socket
s=socket.socket()
s.bind(("",0))
print(s.getsockname()[1])
s.close()')
SERVER="$(cd "$(dirname "$0")" && pwd)/../build/meow-server"
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

echo ""
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
./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo-http >/dev/null 2>&1 || true

FREE_PORT=$(python3 -c 'import socket
s=socket.socket()
s.bind(("",0))
print(s.getsockname()[1])
s.close()')
SERVER="$(cd "$(dirname "$0")" && pwd)/../build/meow-server"
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

echo ""
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
./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo-dual >/dev/null 2>&1 || true

FREE_PORT=$(python3 -c 'import socket
s=socket.socket()
s.bind(("",0))
print(s.getsockname()[1])
s.close()')
SERVER="$(cd "$(dirname "$0")" && pwd)/../build/meow-server"
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

echo ""
echo "=== 31. Repository health state ==="
rm -rf repo-expired
mkdir -p repo-expired/by-name/ex/example/versions
cat > repo-expired/by-name/ex/example/package.toml << EOF
format_version = 1
[metadata]
name = "example"
version = "1.0.0"
architecture = "AMD64"
description = "expired fixture"
EOF
cat > repo-expired/by-name/ex/example/versions/1.0.0.toml << EOF
[artifact]
filename = "example-1.0.0.pkg.tar.zst"
url = "packages/example-1.0.0.pkg.tar.zst"
sha256 = "deadbeef"
EOF
cat > repo-expired/repository.toml << EOF
format_version = 1
name = "expired"
repository_id = "expired-fixture"
generated = "2020-01-01T00:00:00Z"
expires = "2020-01-02T00:00:00Z"
EOF
./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo-expired >/dev/null 2>&1 || true

rm -rf repo-badsig
mkdir -p repo-badsig/by-name/ex/example/versions
cp repo-expired/by-name/ex/example/package.toml repo-badsig/by-name/ex/example/
cp repo-expired/by-name/ex/example/versions/1.0.0.toml repo-badsig/by-name/ex/example/versions/
cat > repo-badsig/repository.toml << EOF
format_version = 1
name = "badsig"
repository_id = "badsig-fixture"
generated = "2024-01-01T00:00:00Z"
expires = "2099-01-01T00:00:00Z"
EOF
./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo-badsig >/dev/null 2>&1 || true
echo "# tampered" >> repo-badsig/repository.toml

rm -rf repo-badmeta
mkdir -p repo-badmeta/by-name/ex/example/versions
printf 'this is not valid toml [[[' > repo-badmeta/repository.toml

cat > meow-health.toml << EOF
[[repositories]]
id = "core"
url = "./repo"
priority = 100

[[repositories]]
id = "testing"
url = "http://127.0.0.1:1/"
priority = 50

[[repositories]]
id = "unstable"
url = "./repo-expired"
priority = 40

[[repositories]]
id = "badsig"
url = "./repo-badsig"
priority = 30

[[repositories]]
id = "badmeta"
url = "./repo-badmeta"
priority = 20
EOF

HEALTH_DB="/tmp/meow-health-$$.db"
HEALTH_OUT=$($MEOW --db-path "$HEALTH_DB" --config meow-health.toml sync 2>&1 | sed 's/\x1b\[[0-9;]*m//g')

if echo "$HEALTH_OUT" | grep -q "core.*Available"; then
    echo "  PASS: valid repository is Available"
    pass=$((pass + 1))
else
    echo "  FAIL: valid repository not reported Available"
    fail=$((fail + 1))
fi

if echo "$HEALTH_OUT" | grep -q "testing.*NetworkError"; then
    echo "  PASS: timeout becomes NetworkError"
    pass=$((pass + 1))
else
    echo "  FAIL: dead endpoint not reported as NetworkError"
    fail=$((fail + 1))
fi

if echo "$HEALTH_OUT" | grep -q "unstable.*Expired"; then
    echo "  PASS: expired metadata becomes Expired"
    pass=$((pass + 1))
else
    echo "  FAIL: expired repo not reported as Expired"
    fail=$((fail + 1))
fi

if echo "$HEALTH_OUT" | grep -q "badsig.*InvalidSignature"; then
    echo "  PASS: bad signature becomes InvalidSignature"
    pass=$((pass + 1))
else
    echo "  FAIL: bad signature not reported as InvalidSignature"
    fail=$((fail + 1))
fi

if echo "$HEALTH_OUT" | grep -q "badmeta.*InvalidMetadata"; then
    echo "  PASS: malformed metadata becomes InvalidMetadata"
    pass=$((pass + 1))
else
    echo "  FAIL: malformed metadata not reported as InvalidMetadata"
    fail=$((fail + 1))
fi

if $MEOW --db-path "$HEALTH_DB" --config meow-health.toml list 2>/dev/null | grep -q "hello"; then
    echo "  PASS: failed repository does not remove healthy repositories"
    pass=$((pass + 1))
else
    echo "  FAIL: healthy repository dropped when others failed"
    fail=$((fail + 1))
fi

rm -f meow-health.toml
git clean -fdq repo-expired repo-badsig repo-badmeta 2>/dev/null || true

echo ""
echo "=== 32. Repository priority selection ==="
makePrioRepo() {
    local dir="$1" name="$2" rid="$3" pkg="$4" ver="$5"
    rm -rf "$dir" "/tmp/prio-src-$pkg"
    mkdir -p "/tmp/prio-src-$pkg/files/usr/bin"
    cat > "/tmp/prio-src-$pkg/package.toml" << EOF
name = "$pkg"
version = "$ver"
architecture = "AMD64"
description = "priority fixture"
EOF
    printf '#!/bin/sh\necho %s\n' "$pkg" > "/tmp/prio-src-$pkg/files/usr/bin/$pkg"
    chmod +x "/tmp/prio-src-$pkg/files/usr/bin/$pkg"
    ./build/meow-build --output "/tmp/prio-artifacts" "/tmp/prio-src-$pkg" >/dev/null 2>&1 || true
    local arch="/tmp/prio-artifacts/$pkg-$ver.pkg.tar.zst"
    local sha
    sha=$(sha256sum "$arch" 2>/dev/null | cut -d' ' -f1)

    mkdir -p "$dir/by-name/${pkg:0:2}/$pkg/versions"
    cat > "$dir/by-name/${pkg:0:2}/$pkg/package.toml" << EOF
format_version = 1
[metadata]
name = "$pkg"
version = "$ver"
architecture = "AMD64"
description = "priority fixture"
EOF
    cat > "$dir/by-name/${pkg:0:2}/$pkg/versions/$ver.toml" << EOF
[artifact]
filename = "$pkg-$ver.pkg.tar.zst"
url = "file://$arch"
sha256 = "$sha"
EOF
    cat > "$dir/repository.toml" << EOF
format_version = 1
name = "$name"
repository_id = "$rid"
generated = "2024-01-01T00:00:00Z"
expires = "2099-01-01T00:00:00Z"
EOF
    ./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo "$dir" >/dev/null 2>&1 || true
}

makePrioRepo prio-core "core" "prio-core-fixture" "hello" "1.0.0"
makePrioRepo prio-testing "testing" "prio-testing-fixture" "hello" "2.0.0"

cat > meow-prio-a.toml << EOF
[[repositories]]
id = "core"
url = "./prio-core"
priority = 100

[[repositories]]
id = "testing"
url = "./prio-testing"
priority = 50
EOF

PRIO_DB="/tmp/meow-prio-a-$$.db"
if $MEOW --db-path "$PRIO_DB" --config meow-prio-a.toml info hello 2>/dev/null | grep -q "Version      1.0.0"; then
    echo "  PASS: higher priority repository wins over newer version"
    pass=$((pass + 1))
else
    echo "  FAIL: priority not honored over version"
    fail=$((fail + 1))
fi

makePrioRepo prio-core-b "core" "prio-core-b-fixture" "hello" "1.0.0"
makePrioRepo prio-testing-b "testing" "prio-testing-b-fixture" "hello" "2.0.0"
cat > meow-prio-b.toml << EOF
[[repositories]]
id = "core"
url = "./prio-core-b"
priority = 100

[[repositories]]
id = "testing"
url = "./prio-testing-b"
priority = 100
EOF

PRIO_DB_B="/tmp/meow-prio-b-$$.db"
if $MEOW --db-path "$PRIO_DB_B" --config meow-prio-b.toml info hello 2>/dev/null | grep -q "Version      2.0.0"; then
    echo "  PASS: same priority chooses highest version"
    pass=$((pass + 1))
else
    echo "  FAIL: tie not broken on newest version"
    fail=$((fail + 1))
fi

makePrioRepo prio-core-c "core" "prio-core-c-fixture" "hello" "1.0.0"
makePrioRepo prio-testing-c "testing" "prio-testing-c-fixture" "world" "3.0.0"
cat > meow-prio-c.toml << EOF
[[repositories]]
id = "core"
url = "./prio-core-c"
priority = 100

[[repositories]]
id = "testing"
url = "./prio-testing-c"
priority = 50
EOF

PRIO_DB_C="/tmp/meow-prio-c-$$.db"
if $MEOW --db-path "$PRIO_DB_C" --config meow-prio-c.toml info world 2>/dev/null | grep -q "Version      3.0.0"; then
    echo "  PASS: lower priority repository used when higher priority lacks package"
    pass=$((pass + 1))
else
    echo "  FAIL: missing package not filled from lower priority"
    fail=$((fail + 1))
fi

makePrioRepo prio-community "community" "prio-community-fixture" "hello" "1.0.0"
cat > meow-prio-d.toml << EOF
[[repositories]]
id = "core"
url = "http://127.0.0.1:1/"
priority = 100

[[repositories]]
id = "community"
url = "./prio-community"
priority = 50
EOF

PRIO_DB_D="/tmp/meow-prio-d-$$.db"
PRIO_D_OUT=$($MEOW --db-path "$PRIO_DB_D" --config meow-prio-d.toml sync 2>&1 | sed 's/\x1b\[[0-9;]*m//g')
if $MEOW --db-path "$PRIO_DB_D" --config meow-prio-d.toml info hello 2>/dev/null | grep -q "Version      1.0.0"; then
    echo "  PASS: unavailable high priority repository does not hide healthy fallback"
    pass=$((pass + 1))
else
    echo "  FAIL: healthy fallback hidden by unavailable high priority"
    fail=$((fail + 1))
fi
if echo "$PRIO_D_OUT" | grep -q "core.*NetworkError"; then
    echo "  PASS: unavailable high priority repository remains visible"
    pass=$((pass + 1))
else
    echo "  FAIL: unavailable core not reported in health table"
    fail=$((fail + 1))
fi

makePrioRepo prio-alpha "alpha" "prio-alpha-fixture" "alpha-pkg" "1.0.0"
makePrioRepo prio-beta "beta" "prio-beta-fixture" "beta-pkg" "2.0.0"
cat > meow-prio-e.toml << EOF
[[repositories]]
id = "alpha"
url = "./prio-alpha"
priority = 100

[[repositories]]
id = "beta"
url = "./prio-beta"
priority = 50
EOF

PRIO_DB_E="/tmp/meow-prio-e-$$.db"
PRIO_E_OUT=$($MEOW --db-path "$PRIO_DB_E" --config meow-prio-e.toml list 2>/dev/null)
if echo "$PRIO_E_OUT" | grep -q "alpha-pkg" && echo "$PRIO_E_OUT" | grep -q "beta-pkg"; then
    echo "  PASS: repository_id cache separation remains intact"
    pass=$((pass + 1))
else
    echo "  FAIL: cache separation lost; merged view incomplete"
    fail=$((fail + 1))
fi

rm -f meow-prio-a.toml meow-prio-b.toml meow-prio-c.toml meow-prio-d.toml meow-prio-e.toml
git clean -fdq prio-core prio-testing prio-core-b prio-testing-b prio-core-c prio-testing-c prio-community prio-alpha prio-beta 2>/dev/null || true

echo ""
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

echo ""
echo "=== 34. Repository mirror failover ==="
rm -rf /tmp/fo-fixture
mkdir -p /tmp/fo-fixture
FO_FIX_PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("",0));print(s.getsockname()[1]);s.close()')
python3 "$(dirname "$0")/http_fixture.py" --root /tmp/fo-fixture --port "$FO_FIX_PORT" >/tmp/fo-fixture.log 2>&1 &
FO_FIX_PID=$!
for _ in $(seq 1 50); do
    if curl -s -o /dev/null "http://127.0.0.1:$FO_FIX_PORT/"; then break; fi
    sleep 0.1
done
FO_FIX="http://127.0.0.1:$FO_FIX_PORT"

makePrioRepo fo-good "fogood" "fo-good-fixture" "hello" "1.0.0"
echo "hello 1.0.0" > fo-good/packages.index
FO_GOOD_PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("",0));print(s.getsockname()[1]);s.close()')
"$(cd "$(dirname "$0")" && pwd)/../build/meow-server" serve ./fo-good --port "$FO_GOOD_PORT" >/tmp/fo-good.log 2>&1 &
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
"$(cd "$(dirname "$0")" && pwd)/../build/meow-server" serve ./fo-bad --port "$FO_BAD_PORT" >/tmp/fo-bad.log 2>&1 &
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

echo ""
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
"$(cd "$(dirname "$0")" && pwd)/../build/meow-server" serve ./fo-good --port "$FO_GOOD_PORT" >/tmp/fo-good.log 2>&1 &
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
"$(cd "$(dirname "$0")" && pwd)/../build/meow-server" serve ./fo-bad --port "$FO_BAD_PORT" >/tmp/fo-bad.log 2>&1 &
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
"$(cd "$(dirname "$0")" && pwd)/../build/meow-server" serve ./fo-foo --port "$FO_FOO_PORT" >/tmp/fo-foo.log 2>&1 &
FO_FOO_PID=$!
"$(cd "$(dirname "$0")" && pwd)/../build/meow-server" serve ./fo-bar --port "$FO_BAR_PORT" >/tmp/fo-bar.log 2>&1 &
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
