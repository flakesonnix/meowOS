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
# Enumerate the environment variable NAMES using only shell builtins (the
# hook PATH is minimal, so no coreutils are assumed). Each name is reported as
# VAR:<name>; the test decides which are required vs forbidden. Shell internals
# such as PWD/SHLVL/_ may or may not be present and are intentionally ignored.
env | while IFS= read -r line; do
    name=${line%%=*}
    echo "VAR:$name"
done
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
# Isolation contract (portable across shells):
#   * every REQUIRED variable is present;
#   * no FORBIDDEN variable (builder/CI secrets) leaked in;
#   * shell internals (PWD/SHLVL/_) are tolerated if present.
REQUIRED="HOME PATH TMPDIR MEOW_PACKAGE MEOW_VERSION MEOW_HOOK_TYPE MEOW_HOOK_STAGING"
abi_ok=yes
for v in $REQUIRED; do
    echo "$out" | grep -q "VAR:$v" || abi_ok=no
done
no_leak=yes
for pat in CI GITHUB_ NIX_ SSH_ XDG_; do
    echo "$out" | grep -q "VAR:$pat" && no_leak=no
done
# Also reject any forbidden prefix appearing as a variable name.
if echo "$out" | grep -Eq "VAR:(CI|GITHUB_|NIX_|SSH_|XDG_)"; then
    no_leak=no
fi
if echo "$out" | grep -q "PKG=hookok VER=1.0.0 TYPE=post_install" && \
   echo "$out" | grep -q "PATH=/usr/bin:/bin" && \
   echo "$out" | grep -q "CWD=.*meow/hooks/hookok/post_install" && \
   [ "$abi_ok" = "yes" ] && [ "$no_leak" = "yes" ]; then
    echo "  PASS: hook runs with isolated cwd + minimal env (required present, no leaks)"
    pass=$((pass + 1))
else
    echo "  FAIL: hook environment not isolated"
    echo "    required_ok=$abi_ok leak_ok=$no_leak"
    echo "    missing: $(for v in $REQUIRED; do echo "$out" | grep -q "VAR:$v" || echo -n "$v "; done)"
    echo "    leaked: $(echo "$out" | grep -oE 'VAR:(CI|GITHUB_|NIX_|SSH_|XDG_)[A-Za-z0-9_]*' | tr '\n' ' ')"
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
echo "=== 25. meow-server static repository hosting ==="
# Start the remote repository server against the existing signed repo/ tree.
SERVER="$(cd "$(dirname "$0")" && pwd)/../build/meow-server"
FREE_PORT=$(python3 -c 'import socket
s=socket.socket()
s.bind(("",0))
print(s.getsockname()[1])
s.close()')
"$SERVER" serve ./repo --port "$FREE_PORT" >/tmp/meow-server.log 2>&1 &
SRV_PID=$!
# Wait for the server to come up.
for _ in $(seq 1 50); do
    if curl -s -o /dev/null "http://127.0.0.1:$FREE_PORT/repository.toml"; then
        break
    fi
    sleep 0.1
done
BASE="http://127.0.0.1:$FREE_PORT"

# 25a. repository.toml is served with TOML content type.
ct=$($MEOW --db-path /dev/null 2>/dev/null; curl -s -D - -o /dev/null "$BASE/repository.toml" | grep -i "content-type" | tr -d '\r')
if echo "$ct" | grep -qi "application/toml"; then
    echo "  PASS: repository.toml served with application/toml"
    pass=$((pass + 1))
else
    echo "  FAIL: repository.toml wrong content-type: $ct"
    fail=$((fail + 1))
fi

# 25b. signature is served.
if curl -s -o /dev/null -w "%{http_code}" "$BASE/repository.toml.sig" | grep -q 200; then
    echo "  PASS: repository.toml.sig served"
    pass=$((pass + 1))
else
    echo "  FAIL: repository.toml.sig not served"
    fail=$((fail + 1))
fi

# 25c. a package manifest is reachable via the sharded by-name layout.
if curl -s -o /dev/null -w "%{http_code}" "$BASE/by-name/he/hello/package.toml" | grep -q 200; then
    echo "  PASS: by-name package manifest served"
    pass=$((pass + 1))
else
    echo "  FAIL: by-name package manifest missing"
    fail=$((fail + 1))
fi

# 25d. a package artifact is served from packages/.
# The fixture repo references artifacts via file:// URLs; stage a copy under
# repo/packages/ so the server can serve it (cleaned up by `git clean repo`).
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

# 25e. range requests return 206 Partial Content for large artifacts.
if curl -s -r 0-15 -o /dev/null -w "%{http_code}" "$BASE/packages/$ART" | grep -q 206; then
    echo "  PASS: range request returns 206 Partial Content"
    pass=$((pass + 1))
else
    echo "  FAIL: range request not supported"
    fail=$((fail + 1))
fi

# 25f. missing path returns 404.
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
# Build a self-contained HTTP repo. Artifact URLs are stored RELATIVE
# (packages/<file>) so the repository is portable across hosts; the HTTP
# backend resolves them against the server base URL. A packages.index
# (newline "name version" pairs) lets the backend enumerate without a
# directory listing, since meow-server is a thin static file server.
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

# 26a. repository.toml is served and loadable over HTTP.
check "load repository.toml over HTTP" "hello" \
    $MEOW --db-path "$HTTP_DB" --repository "$BASE" list

# 26b. signature verification over HTTP (list succeeding implies the
# repository.toml signature was fetched and verified through the same
# trust chain as the filesystem backend).
check "verify HTTP repository signature" "libfoo" \
    $MEOW --db-path "$HTTP_DB" --repository "$BASE" list

# 26c. reject an invalid signature over HTTP. Serve a repo whose
# repository.toml.sig has been corrupted.
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

# 26d. load a package manifest over HTTP.
check "load package manifest over HTTP" "1.1.0" \
    $MEOW --db-path "$HTTP_DB" --repository "$BASE" info hello

# 26e. install a package over the HTTP backend. The result must match a
# filesystem install: same package, same version, files intact.
check "install package over HTTP backend" "done" \
    $MEOW --db-path "$HTTP_DB" --repository "$BASE" install hello
check "hello installed over HTTP" "hello 1.1.0" \
    $MEOW --db-path "$HTTP_DB" installed

# 26f. verify the HTTP-installed package.
check "verify installed package over HTTP" "all files intact" \
    $MEOW --db-path "$HTTP_DB" verify

kill "$SRV_PID" 2>/dev/null || true
wait "$SRV_PID" 2>/dev/null || true

git clean -fdq repo-http repo-http-bad 2>/dev/null || true

echo ""
echo "=== 27. Multiple repositories ==="
# Rebuild an HTTP repo with a deliberate version divergence so we can prove
# repository priority beats version: the HTTP repo carries hello 2.0.0 (a
# higher version than the filesystem ./repo's 1.1.0) but at a lower priority,
# and a package (onlyinhttp) that exists ONLY in the HTTP repo.
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

# Config: stable (filesystem ./repo, priority 100) + testing (http, priority 50)
# + broken (a 404 path on the same server, must not break the others).
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

# 27a. load multiple repositories (merged view lists packages from both).
check "load multiple repositories" "onlyinhttp" \
    $MEOW --db-path "$MULTI_DB" --config meow-multi.toml list
check "load multiple repositories (fs pkg)" "libfoo" \
    $MEOW --db-path "$MULTI_DB" --config meow-multi.toml list

# 27b. filesystem + HTTP mixed (info resolves a package present in both).
check "filesystem + HTTP mixed" "1.1.0" \
    $MEOW --db-path "$MULTI_DB" --config meow-multi.toml info hello

# 27c. repository priority respected: hello 2.0.0 (http) is a higher version
# than 1.1.0 (fs), but stable has higher priority, so 1.1.0 wins.
check "repository priority respected" "hello 1.1.0" \
    $MEOW --db-path "$MULTI_DB" --config meow-multi.toml install hello
check "priority: installed fs version" "hello 1.1.0" \
    $MEOW --db-path "$MULTI_DB" installed

# 27d. package found from secondary (HTTP-only) repo.
check "package found from secondary repo" "done" \
    $MEOW --db-path "$MULTI_DB" --config meow-multi.toml install onlyinhttp
check "secondary repo package installed" "onlyinhttp 1.0.0" \
    $MEOW --db-path "$MULTI_DB" installed

# 27e. cache separated by repository_id: the filesystem repo caches under
# repos/<id> and the HTTP repo under repos-http/<id>.
STABLE_CACHE="$HOME/.cache/meow/repos/./repo"
if [ -d "$STABLE_CACHE" ] && [ -d "$HOME/.cache/meow/repos-http" ] \
   && [ -n "$(ls -A "$HOME/.cache/meow/repos-http" 2>/dev/null)" ]; then
    echo "  PASS: cache separated by repository_id"
    pass=$((pass + 1))
else
    echo "  FAIL: cache not separated by repository_id (stable=$STABLE_CACHE)"
    fail=$((fail + 1))
fi

# 27f. invalid repo does not break healthy repos: list still works despite the
# broken entry, and the broken repo is reported unavailable rather than
# aborting the whole command.
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
echo "Results: $pass passed, $fail failed"
git checkout -- repo 2>/dev/null || true
git clean -fdq repo 2>/dev/null || true
[ "$fail" -eq 0 ] || exit 1
