#!/usr/bin/env bash
# Download robustness (section 15)
set -euo pipefail

run_section() {

echo "=== 15. Download robustness ==="
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
    ./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
}

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

writeHelloVer "file://$HELLO_ARCHIVE" "0000000000000000000000000000000000000000000000000000000000000000"
rm -f ~/.cache/meow/hello-1.1.0.pkg.tar.zst
check "reject checksum mismatch" "ChecksumMismatch" $MEOW --db-path "$TEST_DB" install hello

cp /tmp/hello-ver-bak "$HELLO_VER"
rm -f /tmp/hello-ver-bak
./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
rm -rf "$HELLO_SRC" /tmp/meow-test-pkgs "$HELLO_ARCHIVE"

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
