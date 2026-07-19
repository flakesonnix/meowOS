#!/usr/bin/env bash
# Integration test suite for meow package manager
# Run from repo root: nix develop --command ./test/integration.sh
set -euo pipefail

MEOW="$(cd "$(dirname "$0")" && pwd)/../build/meow"
TEST_DB="/tmp/meow-test-$$.db"
export MEOW TEST_DB

pass=0
fail=0

check() {
    local name="$1" expected="$2"
    shift 2
    local output
    output=$("$@" 2>&1 || true)
    if echo "$output" | grep -qF "$expected"; then
        echo "  PASS: $name"
        pass=$((pass + 1))
    else
        echo "  FAIL: $name"
        echo "    expected: $expected"
        echo "    got: $(echo "$output" | tr '\n' '|')"
        fail=$((fail + 1))
    fi
}

cleanup() {
    rm -f "$TEST_DB" meow.lock
    rm -rf /tmp/meow-install
}

# Build a package archive and print its sha256.
buildPkg() {
    local name="$1" version="$2" binpath="$3" extra="${4:-}"
    local src="/tmp/meow-artifacts/src/$name"
    rm -rf "$src"
    mkdir -p "$src/files/$(dirname "$binpath")"
    cat > "$src/package.toml" << EOF
name = "$name"
version = "$version"
architecture = "AMD64"
description = "integration fixture"
$extra
EOF
    echo "#!/bin/sh" > "$src/files/$binpath"
    chmod +x "$src/files/$binpath"
    ./build/meow-build --output /tmp/meow-artifacts "$src" >/dev/null 2>&1
    local arch="/tmp/meow-artifacts/$name-$version.pkg.tar.zst"
    sha256sum "$arch" | cut -d' ' -f1
}

# Build a package into a given output dir and print its sha256.
# Usage: reproBuild <srcdir> <outdir> <archive-basename> [extra-env...]
reproBuild() {
    local src="$1" out="$2" base="$3"; shift 3
    rm -rf "$out"
    mkdir -p "$out"
    env "$@" ./build/meow-build --output "$out" "$src" >/dev/null 2>&1
    sha256sum "$out/$base" 2>/dev/null | cut -d' ' -f1
}

# Build a package with the given scripts/ content, register it in the
# sample repo under <name>/<version>, and re-sign. Prints nothing.
# Usage: registerHookPkg <name> <version> <scripts-dir>
registerHookPkg() {
    local name="$1" version="$2" scriptsDir="$3"
    local src="/tmp/meow-hook-src-$name"
    rm -rf "$src"
    mkdir -p "$src/files/usr/bin" "$src/scripts"
    printf "#!/bin/sh\necho hi\n" > "$src/files/usr/bin/$name"
    chmod +x "$src/files/usr/bin/$name"
    cp "$scriptsDir"/* "$src/scripts/" 2>/dev/null || true
    cat > "$src/package.toml" << EOF
name = "$name"
version = "$version"
architecture = "AMD64"
description = "hook fixture"
EOF
    ./build/meow-build --output /tmp/meow-artifacts "$src" >/dev/null 2>&1
    local arch="/tmp/meow-artifacts/$name-$version.pkg.tar.zst"
    local sha
    sha=$(sha256sum "$arch" | cut -d' ' -f1)
    mkdir -p "repo/by-name/${name:0:2}/$name/versions"
    cat > "repo/by-name/${name:0:2}/$name/package.toml" << EOF
format_version = 1
[metadata]
name = "$name"
version = "$version"
architecture = "AMD64"
description = "hook fixture"
EOF
    cat > "repo/by-name/${name:0:2}/$name/versions/$version.toml" << EOF
[artifact]
filename = "$name-$version.pkg.tar.zst"
url = "file://$arch"
sha256 = "$sha"
EOF
    ./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
}

# Generate the package archives the sample repo references, then rewrite the
# repo version metadata with the matching sha256 and re-sign. This keeps the
# suite self-contained (no dependency on pre-existing /tmp artifacts).
bootstrapArtifacts() {
    mkdir -p /tmp/meow-artifacts
    local h10 h11 lf a10
    h10=$(buildPkg hello 1.0.0 usr/bin/hello)
    h11=$(buildPkg hello 1.1.0 usr/bin/hello)
    lf=$(buildPkg libfoo 1.0.0 usr/lib/libfoo.so 'provides = ["foo-lib"]')
    a10=$(buildPkg app 1.0.0 usr/bin/app 'depends = ["hello>=1.0.0", "libfoo"]')

    cat > repo/by-name/he/hello/versions/1.0.0.toml << EOF
[artifact]
filename = "hello-1.0.0.pkg.tar.zst"
url = "file:///tmp/meow-artifacts/hello-1.0.0.pkg.tar.zst"
sha256 = "$h10"
EOF
    cat > repo/by-name/he/hello/versions/1.1.0.toml << EOF
[artifact]
filename = "hello-1.1.0.pkg.tar.zst"
url = "file:///tmp/meow-artifacts/hello-1.1.0.pkg.tar.zst"
sha256 = "$h11"
EOF
    cat > repo/by-name/li/libfoo/versions/1.0.0.toml << EOF
[artifact]
filename = "libfoo-1.0.0.pkg.tar.zst"
url = "file:///tmp/meow-artifacts/libfoo-1.0.0.pkg.tar.zst"
sha256 = "$lf"
EOF
    cat > repo/by-name/ap/app/versions/1.0.0.toml << EOF
[artifact]
filename = "app-1.0.0.pkg.tar.zst"
url = "file:///tmp/meow-artifacts/app-1.0.0.pkg.tar.zst"
sha256 = "$a10"
EOF
    # Mirror the built archives into the HTTP fixture root.
    rm -rf /tmp/meow-http-root
    mkdir -p /tmp/meow-http-root
    cp /tmp/meow-artifacts/*.pkg.tar.zst /tmp/meow-http-root/
    ./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
}

HTTP_PID=""
HTTP_PORT=""
startHttp() {
    python3 "$(dirname "$0")/http_fixture.py" --root /tmp/meow-http-root --port 0 >/tmp/meow-http.log 2>&1 &
    HTTP_PID=$!
    for _ in $(seq 1 50); do
        if [ -s /tmp/meow-http.log ]; then
            HTTP_PORT=$(grep -oP 'LISTENING_ON=\K[0-9]+' /tmp/meow-http.log || true)
            [ -n "$HTTP_PORT" ] && break
        fi
        sleep 0.1
    done
    if [ -z "$HTTP_PORT" ]; then
        echo "  FAIL: http fixture server did not start"
        fail=$((fail + 1))
    fi
}

stopHttp() {
    [ -n "$HTTP_PID" ] && kill "$HTTP_PID" 2>/dev/null || true
    wait "$HTTP_PID" 2>/dev/null || true
    HTTP_PID=""
}

# Install trusted key for repo verification
mkdir -p ~/.config/meow/keys
cp "$(dirname "$0")/keys/meow-release.pub" ~/.config/meow/keys/meow-release.pem

cleanup
bootstrapArtifacts

echo "=== 1. Repository queries ==="
check "list shows all packages" "app" $MEOW --db-path "$TEST_DB" list
check "list includes hello" "hello" $MEOW --db-path "$TEST_DB" list
check "list includes libfoo" "libfoo" $MEOW --db-path "$TEST_DB" list

echo "=== 2. Install chain ==="
check "install app (pulls deps)" "done" $MEOW --db-path "$TEST_DB" install app
check "app installed" "app 1.0.0" $MEOW --db-path "$TEST_DB" installed
check "hello installed" "hello 1.1.0" $MEOW --db-path "$TEST_DB" installed
check "libfoo installed" "libfoo 1.0.0" $MEOW --db-path "$TEST_DB" installed

echo "=== 3. Verify integrity ==="
check "verify passes" "all files intact" $MEOW --db-path "$TEST_DB" verify

echo "=== 4. File ownership ==="
check "owns app binary" "app 1.0.0" $MEOW --db-path "$TEST_DB" owns /tmp/meow-install/usr/bin/app
check "owns hello binary" "hello 1.1.0" $MEOW --db-path "$TEST_DB" owns /tmp/meow-install/usr/bin/hello
check "owns libfoo.so" "libfoo 1.0.0" $MEOW --db-path "$TEST_DB" owns /tmp/meow-install/usr/lib/libfoo.so
check "owns unknown file" "no package owns" $MEOW --db-path "$TEST_DB" owns /nonexistent

echo "=== 5. Reverse dependencies ==="
check "hello required-by app" "app" $MEOW --db-path "$TEST_DB" required-by hello
check "libfoo required-by app" "app" $MEOW --db-path "$TEST_DB" required-by libfoo
check "app has no dependents" "nothing depends" $MEOW --db-path "$TEST_DB" required-by app

echo "=== 6. Remove with protection ==="
check "remove libfoo blocked" "cannot remove" $MEOW --db-path "$TEST_DB" remove libfoo

echo "=== 7. Remove app (no blockers) ==="
check "remove app succeeds" "removed app" $MEOW --db-path "$TEST_DB" remove app

echo "=== 8. Remove remaining ==="
check "remove libfoo now ok" "removed libfoo" $MEOW --db-path "$TEST_DB" remove libfoo
check "remove hello now ok" "removed hello" $MEOW --db-path "$TEST_DB" remove hello

echo "=== 9. Empty state ==="
check "no packages installed" "no packages" $MEOW --db-path "$TEST_DB" installed

echo "=== 10. Re-install clean ==="
check "re-install hello" "done" $MEOW --db-path "$TEST_DB" install hello
check "verify after reinstall" "all files intact" $MEOW --db-path "$TEST_DB" verify

cleanup

echo "=== 11. Format version rejection ==="
# 11a. Repository metadata format_version rejection
# Temporarily add format_version = 99 to a metadata package.toml
cp repo/by-name/he/hello/package.toml /tmp/hello-pkg-toml-bak
cat > repo/by-name/he/hello/package.toml << 'EOF'
format_version = 99

[metadata]
name = "hello"
version = "1.1.0"
architecture = "AMD64"
description = "bad"
EOF
check "reject bad repo metadata format" "unsupported package metadata format" $MEOW --db-path "$TEST_DB" list
cp /tmp/hello-pkg-toml-bak repo/by-name/he/hello/package.toml
rm -f /tmp/hello-pkg-toml-bak

# 11b. Repository.toml format_version rejection
cp repo/repository.toml /tmp/repo-toml-bak
cat > repo/repository.toml << 'EOF'
format_version = 99
name = "local"
EOF
check "reject bad repository format" "unsupported repository format" $MEOW --db-path "$TEST_DB" list
cp /tmp/repo-toml-bak repo/repository.toml
rm -f /tmp/repo-toml-bak

# 11c. Lockfile format rejection
cat > /tmp/meow.lock << 'EOF'
lockfile_version = 99
EOF
ln -sf "$PWD/repo" /tmp/repo
OLDPWD=$PWD
cd /tmp
check "reject bad lockfile format" "unsupported lockfile format" $MEOW --db-path /tmp/lock-test.db install --locked hello
cd "$OLDPWD"
rm -f /tmp/meow.lock /tmp/lock-test.db /tmp/repo

# 11d. Database schema version rejection
rm -f /tmp/bad-schema.db
sqlite3 /tmp/bad-schema.db "CREATE TABLE IF NOT EXISTS metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL); INSERT INTO metadata (key, value) VALUES ('schema_version', '99');"
check "reject bad database schema" "unsupported database schema version" $MEOW --db-path /tmp/bad-schema.db list
rm -f /tmp/bad-schema.db

echo "=== 12. Key trust ==="
# 12a. keys list
check "keys list shows meow-release" "meow-release" $MEOW keys list

# 12b. keys add (round-trip)
cp "$(dirname "$0")/keys/meow-release.pub" /tmp/test-add-key.pub
check "keys add works" "added key" $MEOW keys add /tmp/test-add-key.pub
rm -f /tmp/test-add-key.pub /tmp/meow-release.pub

# 12c. Missing key rejection
mv ~/.config/meow/keys/meow-release.pem /tmp/meow-key-bak
check "reject missing trusted key" "TrustedKeyNotFound" $MEOW --db-path "$TEST_DB" list
mv /tmp/meow-key-bak ~/.config/meow/keys/meow-release.pem

# 12d. Bad signature rejection
cp repo/repository.toml /tmp/repo-toml-bak2
echo "# tamper" >> repo/repository.toml
check "reject bad repository signature" "repository signature invalid" $MEOW --db-path "$TEST_DB" list
cp /tmp/repo-toml-bak2 repo/repository.toml
rm -f /tmp/repo-toml-bak2

echo "=== 13. Repository metadata expiry ==="
# 13a. Normal repository (freshly synced + signed) is accepted
check "accept valid repository" "app" $MEOW --db-path "$TEST_DB" list

# 13b. Expired-but-tampered metadata: signature failure happens first
cp repo/repository.toml /tmp/repo-toml-bak3
cp repo/repository.toml.sig /tmp/repo-sig-bak3
yesterday=$(date -u -d 'yesterday' +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date -u -v-1d +%Y-%m-%dT%H:%M:%SZ)
sed -i "s/expires = \".*\"/expires = \"$yesterday\"/" repo/repository.toml
check "signature failure precedes expiry" "repository signature invalid" $MEOW --db-path "$TEST_DB" list
cp /tmp/repo-toml-bak3 repo/repository.toml
cp /tmp/repo-sig-bak3 repo/repository.toml.sig
rm -f /tmp/repo-toml-bak3 /tmp/repo-sig-bak3

# 13c. Correctly signed but expired repository is rejected
cp repo/repository.toml /tmp/repo-toml-bak4
cp repo/repository.toml.sig /tmp/repo-sig-bak4
yesterday=$(date -u -d 'yesterday' +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date -u -v-1d +%Y-%m-%dT%H:%M:%SZ)
sed -i "s/expires = \".*\"/expires = \"$yesterday\"/" repo/repository.toml
./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
check "reject expired repository" "repository metadata expired" $MEOW --db-path "$TEST_DB" list
cp /tmp/repo-toml-bak4 repo/repository.toml
cp /tmp/repo-sig-bak4 repo/repository.toml.sig
rm -f /tmp/repo-toml-bak4 /tmp/repo-sig-bak4

echo "=== 14. Repository identity ==="
# 14a. Valid repository_id is accepted
check "accept repository_id" "app" $MEOW --db-path "$TEST_DB" list

# 14b. Missing repository_id rejected (re-signed so signature passes)
cp repo/repository.toml /tmp/repo-toml-bak5
cp repo/repository.toml.sig /tmp/repo-sig-bak5
sed -i '/^repository_id = /d' repo/repository.toml
./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
check "reject missing repository_id" "InvalidRepository" $MEOW --db-path "$TEST_DB" list
cp /tmp/repo-toml-bak5 repo/repository.toml
cp /tmp/repo-sig-bak5 repo/repository.toml.sig
rm -f /tmp/repo-toml-bak5 /tmp/repo-sig-bak5

# 14c. Invalid characters in repository_id rejected (re-signed)
cp repo/repository.toml /tmp/repo-toml-bak6
cp repo/repository.toml.sig /tmp/repo-sig-bak6
sed -i 's/^repository_id = .*/repository_id = "bad id!"/' repo/repository.toml
./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
check "reject invalid repository_id" "InvalidRepository" $MEOW --db-path "$TEST_DB" list
cp /tmp/repo-toml-bak6 repo/repository.toml
cp /tmp/repo-sig-bak6 repo/repository.toml.sig
rm -f /tmp/repo-toml-bak6 /tmp/repo-sig-bak6

echo "=== 15. Download robustness ==="
# Build a real package archive to drive offline download scenarios.
HELLO_SRC=/tmp/meow-hello-src
HELLO_ARCHIVE=/tmp/meow-hello-1.1.0.pkg.tar.zst
rm -rf "$HELLO_SRC" /tmp/meow-test-pkgs
mkdir -p "$HELLO_SRC/files/usr/bin"
cat > "$HELLO_SRC/package.toml" << 'EOF'
name = "hello"
version = "1.1.0"
architecture = "AMD64"
description = "download test fixture"
EOF
echo '#!/bin/sh' > "$HELLO_SRC/files/usr/bin/hello"
chmod +x "$HELLO_SRC/files/usr/bin/hello"
mkdir -p /tmp/meow-test-pkgs
./build/meow-build --output /tmp/meow-test-pkgs "$HELLO_SRC" >/dev/null 2>&1 || true
mv /tmp/meow-test-pkgs/hello-1.1.0.pkg.tar.zst "$HELLO_ARCHIVE" 2>/dev/null || true
HELLO_SHA=$(sha256sum "$HELLO_ARCHIVE" | cut -d' ' -f1)

HELLO_VER="repo/by-name/he/hello/versions/1.1.0.toml"
writeHelloVer() {
    local url="$1" sha="$2"
    cat > "$HELLO_VER" << EOF
[artifact]
filename = "hello-1.1.0.pkg.tar.zst"
url = "$url"
sha256 = "$sha"
EOF
    ./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
}

# 15a. Successful install leaves no partial (.part) files (atomic rename)
cp "$HELLO_VER" /tmp/hello-ver-bak
writeHelloVer "file://$HELLO_ARCHIVE" "$HELLO_SHA"
rm -rf /tmp/meow-install
rm -f ~/.cache/meow/hello-1.1.0.pkg.tar.zst
check "install hello (atomic)" "done" $MEOW --db-path "$TEST_DB" install hello
if find /tmp/meow-install -name '*.part' 2>/dev/null | grep -q .; then
    echo "  FAIL: leftover .part files after install"
    fail=$((fail + 1))
else
    echo "  PASS: no partial files after install"
    pass=$((pass + 1))
fi
$MEOW --db-path "$TEST_DB" remove hello >/dev/null 2>&1 || true

# 15b. HTTP download failure is reported and leaves no partial file
writeHelloVer "http://127.0.0.1:9/missing.tar.zst" "$HELLO_SHA"
rm -rf /tmp/meow-install
rm -f ~/.cache/meow/hello-1.1.0.pkg.tar.zst
check "reject failed download" "DownloadFailed" $MEOW --db-path "$TEST_DB" install hello
if find /tmp/meow-install -name '*.part' 2>/dev/null | grep -q .; then
    echo "  FAIL: leftover .part file after failed download"
    fail=$((fail + 1))
else
    echo "  PASS: no partial file after failed download"
    pass=$((pass + 1))
fi

# 15c. Checksum mismatch is rejected (offline file:// with wrong sha)
writeHelloVer "file://$HELLO_ARCHIVE" "0000000000000000000000000000000000000000000000000000000000000000"
rm -f ~/.cache/meow/hello-1.1.0.pkg.tar.zst
check "reject checksum mismatch" "ChecksumMismatch" $MEOW --db-path "$TEST_DB" install hello

# restore good version + signature
cp /tmp/hello-ver-bak "$HELLO_VER"
rm -f /tmp/hello-ver-bak
./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
rm -rf "$HELLO_SRC" /tmp/meow-test-pkgs "$HELLO_ARCHIVE"

echo "=== 16. HTTP transport paths (local fixture server) ==="
startHttp
if [ -n "$HTTP_PORT" ]; then
    HTTP_BASE="http://127.0.0.1:${HTTP_PORT}"
    APP_VER="repo/by-name/ap/app/versions/1.0.0.toml"
    LIB_VER="repo/by-name/li/libfoo/versions/1.0.0.toml"

    # 16a. Successful HTTP download (transport exercises libcurl).
    cp "$APP_VER" /tmp/app-ver-bak
    a10=$(sha256sum /tmp/meow-http-root/app-1.0.0.pkg.tar.zst | cut -d' ' -f1)
    cat > "$APP_VER" << EOF
[artifact]
filename = "app-1.0.0.pkg.tar.zst"
url = "$HTTP_BASE/app-1.0.0.pkg.tar.zst"
sha256 = "$a10"
EOF
    ./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
    rm -rf /tmp/meow-install
    rm -f ~/.cache/meow/app-1.0.0.pkg.tar.zst
    check "http download success" "done" $MEOW --db-path "$TEST_DB" install app
    $MEOW --db-path "$TEST_DB" remove app >/dev/null 2>&1 || true

    # 16b. HTTP 404 is reported as DownloadHttpError.
    cat > "$APP_VER" << EOF
[artifact]
filename = "app-1.0.0.pkg.tar.zst"
url = "$HTTP_BASE/missing.pkg.tar.zst"
sha256 = "$a10"
EOF
    ./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
    rm -rf /tmp/meow-install
    rm -f ~/.cache/meow/app-1.0.0.pkg.tar.zst
    check "http 404 rejected" "DownloadHttpError" $MEOW --db-path "$TEST_DB" install app

    # 16c. Retry on transient 5xx: /flaky returns 500 twice then 200.
    cat > "$APP_VER" << EOF
[artifact]
filename = "app-1.0.0.pkg.tar.zst"
url = "$HTTP_BASE/flaky/app-1.0.0.pkg.tar.zst"
sha256 = "$a10"
EOF
    ./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
    rm -rf /tmp/meow-install
    rm -f ~/.cache/meow/app-1.0.0.pkg.tar.zst
    check "http retry on 5xx" "done" $MEOW --db-path "$TEST_DB" install app
    $MEOW --db-path "$TEST_DB" remove app >/dev/null 2>&1 || true

    # 16d. Timeout: /slow sleeps longer than the default 30s timeout.
    cat > "$APP_VER" << EOF
[artifact]
filename = "app-1.0.0.pkg.tar.zst"
url = "$HTTP_BASE/slow/app-1.0.0.pkg.tar.zst"
sha256 = "$a10"
EOF
    ./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
    rm -rf /tmp/meow-install
    rm -f ~/.cache/meow/app-1.0.0.pkg.tar.zst
    check "http timeout" "DownloadTimeout" $MEOW --db-path "$TEST_DB" install app

    # restore good version + signature
    cp /tmp/app-ver-bak "$APP_VER"
    rm -f /tmp/app-ver-bak
    ./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
    stopHttp
fi

echo ""
echo "=== 17. Parallel package downloads ==="
startHttp
if [ -n "$HTTP_PORT" ]; then
    HTTP_BASE="http://127.0.0.1:${HTTP_PORT}"
    APP_VER="repo/by-name/ap/app/versions/1.0.0.toml"
    HELLO_VER="repo/by-name/he/hello/versions/1.1.0.toml"
    LIB_VER="repo/by-name/li/libfoo/versions/1.0.0.toml"

    a10=$(sha256sum /tmp/meow-http-root/app-1.0.0.pkg.tar.zst | cut -d' ' -f1)
    h11=$(sha256sum /tmp/meow-http-root/hello-1.1.0.pkg.tar.zst | cut -d' ' -f1)
    lf=$(sha256sum /tmp/meow-http-root/libfoo-1.0.0.pkg.tar.zst | cut -d' ' -f1)

    # Point every artifact at the HTTP fixture so install fetches them in
    # parallel (app pulls hello + libfoo).
    cp "$APP_VER" /tmp/app-ver-bak2
    cat > "$APP_VER" << EOF
[artifact]
filename = "app-1.0.0.pkg.tar.zst"
url = "$HTTP_BASE/app-1.0.0.pkg.tar.zst"
sha256 = "$a10"
EOF
    cat > "$HELLO_VER" << EOF
[artifact]
filename = "hello-1.1.0.pkg.tar.zst"
url = "$HTTP_BASE/hello-1.1.0.pkg.tar.zst"
sha256 = "$h11"
EOF
    cat > "$LIB_VER" << EOF
[artifact]
filename = "libfoo-1.0.0.pkg.tar.zst"
url = "$HTTP_BASE/libfoo-1.0.0.pkg.tar.zst"
sha256 = "$lf"
EOF
    ./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true

    # 17a. All three artifacts end up in the cache (concurrent writes).
    rm -rf /tmp/meow-install
    rm -f ~/.cache/meow/app-1.0.0.pkg.tar.zst \
          ~/.cache/meow/hello-1.1.0.pkg.tar.zst \
          ~/.cache/meow/libfoo-1.0.0.pkg.tar.zst
    check "parallel install writes all caches" "done" $MEOW --db-path "$TEST_DB" install app
    cached=0
    for f in app-1.0.0 hello-1.1.0 libfoo-1.0.0; do
        [ -f ~/.cache/meow/$f.pkg.tar.zst ] && cached=$((cached + 1))
    done
    if [ "$cached" -eq 3 ]; then
        echo "  PASS: all artifacts cached after parallel download"
        pass=$((pass + 1))
    else
        echo "  FAIL: only $cached/3 artifacts cached after parallel download"
        fail=$((fail + 1))
    fi
    $MEOW --db-path "$TEST_DB" remove app >/dev/null 2>&1 || true
    $MEOW --db-path "$TEST_DB" remove libfoo >/dev/null 2>&1 || true
    $MEOW --db-path "$TEST_DB" remove hello >/dev/null 2>&1 || true

    # 17b. A failed artifact cancels the batch and cleans partial files.
    cat > "$LIB_VER" << EOF
[artifact]
filename = "libfoo-1.0.0.pkg.tar.zst"
url = "$HTTP_BASE/missing-libfoo.pkg.tar.zst"
sha256 = "$lf"
EOF
    ./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
    rm -rf /tmp/meow-install
    rm -f ~/.cache/meow/app-1.0.0.pkg.tar.zst \
          ~/.cache/meow/hello-1.1.0.pkg.tar.zst \
          ~/.cache/meow/libfoo-1.0.0.pkg.tar.zst
    check "failed batch cancels" "DownloadHttpError" $MEOW --db-path "$TEST_DB" install app
    if find ~/.cache/meow -name '*.part' 2>/dev/null | grep -q .; then
        echo "  FAIL: leftover .part files after failed parallel batch"
        fail=$((fail + 1))
    else
        echo "  PASS: no partial files after failed parallel batch"
        pass=$((pass + 1))
    fi

    # restore good versions + signature
    cp /tmp/app-ver-bak2 "$APP_VER"
    rm -f /tmp/app-ver-bak2
    cat > "$HELLO_VER" << EOF
[artifact]
filename = "hello-1.1.0.pkg.tar.zst"
url = "file:///tmp/meow-artifacts/hello-1.1.0.pkg.tar.zst"
sha256 = "$h11"
EOF
    cat > "$LIB_VER" << EOF
[artifact]
filename = "libfoo-1.0.0.pkg.tar.zst"
url = "file:///tmp/meow-artifacts/libfoo-1.0.0.pkg.tar.zst"
sha256 = "$lf"
EOF
    ./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
    stopHttp
fi

echo ""
echo "=== 18. doctor diagnostics ==="
# doctor must run and emit its category sections regardless of environment.
check "doctor runs" "meow doctor:" $MEOW --db-path "$TEST_DB" doctor
# JSON variant must contain the checks array.
check "doctor --json has checks" '"checks": [' $MEOW --db-path "$TEST_DB" doctor --json

echo ""
echo "=== 19. Reproducible package generation ==="
REPRO_SRC="/tmp/meow-repro-src"
rm -rf "$REPRO_SRC"
mkdir -p "$REPRO_SRC/files/usr/bin" "$REPRO_SRC/files/etc"
printf "#!/bin/sh\necho hi\n" > "$REPRO_SRC/files/usr/bin/hello"
chmod +x "$REPRO_SRC/files/usr/bin/hello"
echo "config" > "$REPRO_SRC/files/etc/hello.conf"
cat > "$REPRO_SRC/package.toml" << 'EOF'
name = "hello"
version = "1.0.0"
architecture = "AMD64"
description = "reproducibility fixture"

[build]
reproducible = true
source_date_epoch = 1700000000
EOF

# 19a. Building the same source twice yields the same artifact hash.
h_a=$(reproBuild "$REPRO_SRC" /tmp/meow-repro-a hello-1.0.0.pkg.tar.zst)
h_b=$(reproBuild "$REPRO_SRC" /tmp/meow-repro-b hello-1.0.0.pkg.tar.zst)
if [ "$h_a" = "$h_b" ] && [ -n "$h_a" ]; then
    echo "  PASS: same source builds identical artifact"
    pass=$((pass + 1))
else
    echo "  FAIL: repeated build differs ($h_a vs $h_b)"
    fail=$((fail + 1))
fi

# 19b. Different on-disk mtime does not change the artifact hash.
touch -d "1999-01-01 00:00:00" "$REPRO_SRC/files/usr/bin/hello" "$REPRO_SRC/files/etc/hello.conf"
h_m=$(reproBuild "$REPRO_SRC" /tmp/meow-repro-m hello-1.0.0.pkg.tar.zst)
if [ "$h_m" = "$h_a" ]; then
    echo "  PASS: source mtime does not affect artifact"
    pass=$((pass + 1))
else
    echo "  FAIL: mtime changed artifact ($h_m vs $h_a)"
    fail=$((fail + 1))
fi

# 19c. Different on-disk file order does not change the artifact hash.
#     Recreate the tree so directory iteration order may differ.
rm -rf "$REPRO_SRC/files"
mkdir -p "$REPRO_SRC/files/etc" "$REPRO_SRC/files/usr/bin"
echo "config" > "$REPRO_SRC/files/etc/hello.conf"
printf "#!/bin/sh\necho hi\n" > "$REPRO_SRC/files/usr/bin/hello"
chmod +x "$REPRO_SRC/files/usr/bin/hello"
h_o=$(reproBuild "$REPRO_SRC" /tmp/meow-repro-o hello-1.0.0.pkg.tar.zst)
if [ "$h_o" = "$h_a" ]; then
    echo "  PASS: source file order does not affect artifact"
    pass=$((pass + 1))
else
    echo "  FAIL: file order changed artifact ($h_o vs $h_a)"
    fail=$((fail + 1))
fi

# 19d. A different SOURCE_DATE_EPOCH produces a different (but still
#     deterministic) artifact hash. Use a fixture that does NOT pin
#     source_date_epoch in [build], so the env var is the active source.
REPRO_ENV="/tmp/meow-repro-env"
rm -rf "$REPRO_ENV"
mkdir -p "$REPRO_ENV/files/usr/bin"
printf "#!/bin/sh\necho hi\n" > "$REPRO_ENV/files/usr/bin/hello"
chmod +x "$REPRO_ENV/files/usr/bin/hello"
cat > "$REPRO_ENV/package.toml" << 'EOF'
name = "hello"
version = "1.0.0"
architecture = "AMD64"
description = "reproducibility fixture"
EOF
h_default=$(reproBuild "$REPRO_ENV" /tmp/meow-repro-d hello-1.0.0.pkg.tar.zst)
h_e=$(reproBuild "$REPRO_ENV" /tmp/meow-repro-e hello-1.0.0.pkg.tar.zst SOURCE_DATE_EPOCH=1720000000)
if [ -n "$h_e" ] && [ -n "$h_default" ] && [ "$h_e" != "$h_default" ]; then
    echo "  PASS: SOURCE_DATE_EPOCH override changes hash deterministically"
    pass=$((pass + 1))
else
    echo "  FAIL: SOURCE_DATE_EPOCH had no effect ($h_e vs $h_default)"
    fail=$((fail + 1))
fi
rm -rf "$REPRO_ENV" /tmp/meow-repro-d

# 19e. build.json metadata is embedded and self-describing.
if ./build/meow-build --output /tmp/meow-repro-x "$REPRO_SRC" >/dev/null 2>&1 && \
   tar --zstd -tf /tmp/meow-repro-x/hello-1.0.0.pkg.tar.zst 2>/dev/null | grep -q "metadata/build.json"; then
    echo "  PASS: metadata/build.json embedded in archive"
    pass=$((pass + 1))
else
    echo "  FAIL: metadata/build.json missing from archive"
    fail=$((fail + 1))
fi

rm -rf "$REPRO_SRC" /tmp/meow-repro-a /tmp/meow-repro-b /tmp/meow-repro-m \
       /tmp/meow-repro-o /tmp/meow-repro-e /tmp/meow-repro-x

echo ""
echo "=== 20. Restricted hook runner ==="
KEY="$(dirname "$0")/keys/meow-release.pem"

# 20a. Successful hook runs in an isolated cwd with a minimal environment,
#     and its output is captured.
HOOK_OK="/tmp/meow-hook-ok"
rm -rf "$HOOK_OK"
mkdir -p "$HOOK_OK"
cat > "$HOOK_OK/post_install" << 'EOF'
#!/bin/sh
echo "PKG=$MEOW_PACKAGE VER=$MEOW_VERSION TYPE=$MEOW_HOOK_TYPE"
echo "PATH=$PATH"
echo "CWD=$(pwd)"
EOF
chmod +x "$HOOK_OK/post_install"
registerHookPkg hookok 1.0.0 "$HOOK_OK"
rm -rf /tmp/meow-hook-install
out=$(MEOW_HOOK_TIMEOUT=30 $MEOW --db-path "$TEST_DB" install hookok 2>&1)
if echo "$out" | grep -q "done"; then
    echo "  PASS: successful hook install completes"
    pass=$((pass + 1))
else
    echo "  FAIL: successful hook install failed"
    fail=$((fail + 1))
fi
if echo "$out" | grep -q "PKG=hookok VER=1.0.0 TYPE=post_install" && \
   echo "$out" | grep -q "PATH=/usr/bin:/bin" && \
   echo "$out" | grep -q "CWD=/tmp/nix-shell"; then
    echo "  PASS: hook runs with isolated cwd + minimal env"
    pass=$((pass + 1))
else
    echo "  FAIL: hook environment not isolated"
    fail=$((fail + 1))
fi
if echo "$out" | grep -q "post_install output:"; then
    echo "  PASS: hook output captured"
    pass=$((pass + 1))
else
    echo "  FAIL: hook output not captured"
    fail=$((fail + 1))
fi
$MEOW --db-path "$TEST_DB" remove hookok >/dev/null 2>&1 || true

# 20b. A failing hook aborts the install and rolls back (package absent).
HOOK_BAD="/tmp/meow-hook-bad"
rm -rf "$HOOK_BAD"
mkdir -p "$HOOK_BAD"
cat > "$HOOK_BAD/post_install" << 'EOF'
#!/bin/sh
echo "boom"
exit 3
EOF
chmod +x "$HOOK_BAD/post_install"
registerHookPkg hookbad 1.0.0 "$HOOK_BAD"
rm -rf /tmp/meow-hook-install
out=$($MEOW --db-path "$TEST_DB" install hookbad 2>&1 || true)
if echo "$out" | grep -q "HookFailed"; then
    echo "  PASS: failing hook reported as HookFailed"
    pass=$((pass + 1))
else
    echo "  FAIL: failing hook not reported"
    fail=$((fail + 1))
fi
if ! $MEOW --db-path "$TEST_DB" installed 2>/dev/null | grep -q "hookbad"; then
    echo "  PASS: failed hook rolled back install"
    pass=$((pass + 1))
else
    echo "  FAIL: hookbad present after failed hook"
    fail=$((fail + 1))
fi
$MEOW --db-path "$TEST_DB" remove hookbad >/dev/null 2>&1 || true

# 20c. A long-running hook is killed by the timeout (SIGTERM then SIGKILL).
HOOK_SLOW="/tmp/meow-hook-slow"
rm -rf "$HOOK_SLOW"
mkdir -p "$HOOK_SLOW"
cat > "$HOOK_SLOW/post_install" << 'EOF'
#!/bin/sh
# Busy-loop (PATH-independent) so it outlives the timeout without relying
# on any external binary in the minimal hook PATH.
while true; do :; done
EOF
chmod +x "$HOOK_SLOW/post_install"
registerHookPkg hookslow 1.0.0 "$HOOK_SLOW"
rm -rf /tmp/meow-hook-install
out=$(MEOW_HOOK_TIMEOUT=1 $MEOW --db-path "$TEST_DB" install hookslow 2>&1 || true)
if echo "$out" | grep -q "HookTimeout"; then
    echo "  PASS: runaway hook killed by timeout"
    pass=$((pass + 1))
else
    echo "  FAIL: runaway hook not timed out"
    fail=$((fail + 1))
fi
$MEOW --db-path "$TEST_DB" remove hookslow >/dev/null 2>&1 || true

rm -rf "$HOOK_OK" "$HOOK_BAD" "$HOOK_SLOW" /tmp/meow-hook-src-* 

echo ""
echo "=== 21. doctor --security ==="
# Security report must run and cover the key areas.
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
# A stale .part file in the cache must be flagged as a warning.
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

echo ""
echo "=== 22. fresh-install from an empty system ==="
# Simulate a brand-new machine: no database, no installed packages.
# The trusted key anchor is already in place (~/.config/meow/keys, line 170).
FRESH_DB="$TMPDIR/fresh-$$.db"
rm -f "$FRESH_DB"
# Install a package and verify it lands cleanly.
if $MEOW --db-path "$FRESH_DB" install hello >/dev/null 2>&1; then
    if $MEOW --db-path "$FRESH_DB" installed | grep -q hello && \
       $MEOW --db-path "$FRESH_DB" verify | grep -q "verified" && \
       $MEOW --db-path "$FRESH_DB" doctor --security >/dev/null 2>&1; then
        echo "  PASS: fresh install -> installed -> verify -> doctor --security"
        pass=$((pass + 1))
    else
        echo "  FAIL: fresh-install chain did not complete cleanly"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: fresh install failed"
    fail=$((fail + 1))
fi
rm -f "$FRESH_DB"

echo ""
echo "=== 23. upgrade from a v0.3.0 database ==="
# A v0.3.0 database has no 'sha256' column on files and no schema metadata.
# Opening it with the v0.4.0 binary must migrate transparently and preserve
# the installed package without data loss.
UPG_DB="$TMPDIR/upgrade-$$.db"
rm -f "$UPG_DB"
python3 - "$UPG_DB" <<'PY'
import sqlite3, sys
db = sys.argv[1]
c = sqlite3.connect(db)
c.executescript("""
CREATE TABLE packages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    version TEXT NOT NULL,
    architecture TEXT NOT NULL,
    install_time INTEGER NOT NULL
);
CREATE TABLE files (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    package_id INTEGER NOT NULL,
    path TEXT NOT NULL,
    size INTEGER DEFAULT 0,
    FOREIGN KEY (package_id) REFERENCES packages(id) ON DELETE CASCADE
);
CREATE TABLE package_deps (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    package_id INTEGER NOT NULL,
    dep_name TEXT NOT NULL,
    FOREIGN KEY (package_id) REFERENCES packages(id) ON DELETE CASCADE
);
CREATE TABLE package_provides (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    package_id INTEGER NOT NULL,
    provide_name TEXT NOT NULL,
    FOREIGN KEY (package_id) REFERENCES packages(id) ON DELETE CASCADE
);
""")
c.execute("INSERT INTO packages (name,version,architecture,install_time) VALUES ('legacy-pkg','1.0.0','AMD64',1704067200)")
c.execute("INSERT INTO files (package_id,path,size) VALUES (1,'/usr/bin/legacy',42)")
c.commit(); c.close()
PY
installed_ok=$($MEOW --db-path "$UPG_DB" installed 2>&1 | grep -q legacy-pkg && echo yes || echo no)
verify_ok=$($MEOW --db-path "$UPG_DB" verify 2>&1 | grep -q "legacy-pkg" && echo yes || echo no)
doc_out=$($MEOW --db-path "$UPG_DB" doctor 2>&1 || true)
doctor_ok=$(echo "$doc_out" | grep -q "database" && echo yes || echo no)
if [ "$installed_ok" = "yes" ] && [ "$verify_ok" = "yes" ] && [ "$doctor_ok" = "yes" ]; then
    echo "  PASS: v0.3 DB migrated, legacy-pkg preserved, verify + doctor ran"
    pass=$((pass + 1))
else
    echo "  FAIL: v0.3 -> v0.4 upgrade lost data or failed (installed=$installed_ok verify=$verify_ok doctor=$doctor_ok)"
    echo "    UPG_DB exists: $(test -f "$UPG_DB" && echo yes || echo no)"
    echo "    doctor output: $(echo "$doc_out" | tr '\n' '|')"
    fail=$((fail + 1))
fi
rm -f "$UPG_DB"

echo ""
echo "=== 24. reproducibility release check ==="
# Building the same source twice must produce a byte-identical artifact.
REL_SRC="/tmp/meow-rel-src"
rm -rf "$REL_SRC"
mkdir -p "$REL_SRC/files/usr/bin"
printf "#!/bin/sh\necho hi\n" > "$REL_SRC/files/usr/bin/hello"
chmod +x "$REL_SRC/files/usr/bin/hello"
cat > "$REL_SRC/package.toml" << 'EOF'
name = "hello"
version = "1.0.0"
architecture = "AMD64"
description = "reproducibility release fixture"

[build]
reproducible = true
source_date_epoch = 1700000000
EOF
h1=$(reproBuild "$REL_SRC" /tmp/meow-rel-a hello-1.0.0.pkg.tar.zst || true)
h2=$(reproBuild "$REL_SRC" /tmp/meow-rel-b hello-1.0.0.pkg.tar.zst || true)
if [ -n "$h1" ] && [ "$h1" = "$h2" ]; then
    echo "  PASS: reproducible build is byte-identical ($h1)"
    pass=$((pass + 1))
else
    echo "  FAIL: reproducible build differs ($h1 vs $h2)"
    fail=$((fail + 1))
fi
rm -rf "$REL_SRC"

echo ""
echo "Results: $pass passed, $fail failed"
git checkout -- repo 2>/dev/null || true
git clean -fdq repo 2>/dev/null || true
[ "$fail" -eq 0 ] || exit 1
