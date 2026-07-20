#!/usr/bin/env bash
# HTTP transport and parallel downloads (sections 16-17)
set -euo pipefail

run_section() {

echo "=== 16. HTTP transport paths (local fixture server) ==="
startHttp
if [ -n "$HTTP_PORT" ]; then
    HTTP_BASE="http://127.0.0.1:${HTTP_PORT}"
    APP_VER="repo/by-name/ap/app/versions/1.0.0.toml"
    LIB_VER="repo/by-name/li/libfoo/versions/1.0.0.toml"

    cp "$APP_VER" /tmp/app-ver-bak
    a10=$(sha256sum /tmp/meow-http-root/app-1.0.0.pkg.tar.zst | cut -d' ' -f1)
    cat > "$APP_VER" << EOF
[artifact]
filename = "app-1.0.0.pkg.tar.zst"
url = "$HTTP_BASE/app-1.0.0.pkg.tar.zst"
sha256 = "$a10"
EOF
    ./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
    rm -rf /tmp/meow-install
    rm -f ~/.cache/meow/app-1.0.0.pkg.tar.zst
    check "http download success" "done" $MEOW --db-path "$TEST_DB" install app
    $MEOW --db-path "$TEST_DB" remove app >/dev/null 2>&1 || true

    cat > "$APP_VER" << EOF
[artifact]
filename = "app-1.0.0.pkg.tar.zst"
url = "$HTTP_BASE/missing.pkg.tar.zst"
sha256 = "$a10"
EOF
    ./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
    rm -rf /tmp/meow-install
    rm -f ~/.cache/meow/app-1.0.0.pkg.tar.zst
    check "http 404 rejected" "DownloadHttpError" $MEOW --db-path "$TEST_DB" install app

    cat > "$APP_VER" << EOF
[artifact]
filename = "app-1.0.0.pkg.tar.zst"
url = "$HTTP_BASE/flaky/app-1.0.0.pkg.tar.zst"
sha256 = "$a10"
EOF
    ./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
    rm -rf /tmp/meow-install
    rm -f ~/.cache/meow/app-1.0.0.pkg.tar.zst
    check "http retry on 5xx" "done" $MEOW --db-path "$TEST_DB" install app
    $MEOW --db-path "$TEST_DB" remove app >/dev/null 2>&1 || true

    cat > "$APP_VER" << EOF
[artifact]
filename = "app-1.0.0.pkg.tar.zst"
url = "$HTTP_BASE/slow/app-1.0.0.pkg.tar.zst"
sha256 = "$a10"
EOF
    ./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
    rm -rf /tmp/meow-install
    rm -f ~/.cache/meow/app-1.0.0.pkg.tar.zst
    check "http timeout" "DownloadTimeout" $MEOW --db-path "$TEST_DB" install app

    cp /tmp/app-ver-bak "$APP_VER"
    rm -f /tmp/app-ver-bak
    ./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
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
    ./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true

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

    cat > "$LIB_VER" << EOF
[artifact]
filename = "libfoo-1.0.0.pkg.tar.zst"
url = "$HTTP_BASE/missing-libfoo.pkg.tar.zst"
sha256 = "$lf"
EOF
    ./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
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
    ./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
    stopHttp
fi

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
