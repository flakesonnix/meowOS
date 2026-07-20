# Common helpers and setup for the integration test suite.
# Sourced by test/integration.sh (not standalone).

MEOW="$(cd "$(dirname "$0")/.." && pwd)/build/meow"
TEST_DB="/tmp/meow-test-$$.db"
export MEOW TEST_DB
: "${TMPDIR:=/tmp}"

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
    local name="$1" version="$2" binpath="$3" extra="${4:-}" optdeps="${5:-}"
    local src="/tmp/meow-artifacts/src/$name"
    rm -rf "$src"
    mkdir -p "$src/files/$(dirname "$binpath")"
    cat > "$src/package.toml" << EOF
name = "$name"
version = "$version"
architecture = "AMD64"
description = "integration fixture"
$extra
$optdeps
EOF
    echo "#!/bin/sh" > "$src/files/$binpath"
    chmod +x "$src/files/$binpath"
    ./build/meow-build --output /tmp/meow-artifacts "$src" >/dev/null 2>&1
    local arch="/tmp/meow-artifacts/$name-$version.pkg.tar.zst"
    sha256sum "$arch" | cut -d' ' -f1
}

registerOptPkg() {
    local name="$1" version="$2" binpath="$3"
    local sha
    sha=$(buildPkg "$name" "$version" "$binpath")
    mkdir -p "repo/by-name/${name:0:2}/$name/versions"
    cat > "repo/by-name/${name:0:2}/$name/package.toml" << EOF
format_version = 1
[metadata]
name = "$name"
version = "$version"
architecture = "AMD64"
description = "optional fixture"
EOF
    cat > "repo/by-name/${name:0:2}/$name/versions/$version.toml" << EOF
[artifact]
filename = "$name-$version.pkg.tar.zst"
url = "file:///tmp/meow-artifacts/$name-$version.pkg.tar.zst"
sha256 = "$sha"
EOF
    ./build/meow-repo sign --key "$(dirname "$0")/keys/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
}

reproBuild() {
    local src="$1" out="$2" base="$3"; shift 3
    rm -rf "$out"
    mkdir -p "$out"
    env "$@" ./build/meow-build --output "$out" "$src" >/dev/null 2>&1
    sha256sum "$out/$base" 2>/dev/null | cut -d' ' -f1
}

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

bootstrapArtifacts() {
    mkdir -p /tmp/meow-artifacts
    local h10 h11 lf a10 lb10 lb20 lb30 mya mx
    h10=$(buildPkg hello 1.0.0 usr/bin/hello)
    h11=$(buildPkg hello 1.1.0 usr/bin/hello)
    lf=$(buildPkg libfoo 1.0.0 usr/lib/libfoo.so 'provides = ["foo-lib"]')
    a10=$(buildPkg app 1.0.0 usr/bin/app 'depends = ["hello>=1.0.0", "libfoo"]')
    lb10=$(buildPkg libbar 1.0.0 usr/lib/libbar.so)
    lb20=$(buildPkg libbar 2.0.0 usr/lib/libbar.so)
    lb30=$(buildPkg libbar 3.0.0 usr/lib/libbar.so)
    mya=$(buildPkg myapp 1.0.0 usr/bin/myapp 'depends = ["libbar>=2.0"]')
    mx=$(buildPkg myapp-exact 1.0.0 usr/bin/myapp-exact 'depends = ["libbar=1.0"]')

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
    mkdir -p repo/by-name/li/libbar/versions \
             repo/by-name/my/myapp/versions \
             repo/by-name/my/myapp-exact/versions
    cat > repo/by-name/li/libbar/versions/1.0.0.toml << EOF
[artifact]
filename = "libbar-1.0.0.pkg.tar.zst"
url = "file:///tmp/meow-artifacts/libbar-1.0.0.pkg.tar.zst"
sha256 = "$lb10"
EOF
    cat > repo/by-name/li/libbar/versions/2.0.0.toml << EOF
[artifact]
filename = "libbar-2.0.0.pkg.tar.zst"
url = "file:///tmp/meow-artifacts/libbar-2.0.0.pkg.tar.zst"
sha256 = "$lb20"
EOF
    cat > repo/by-name/li/libbar/versions/3.0.0.toml << EOF
[artifact]
filename = "libbar-3.0.0.pkg.tar.zst"
url = "file:///tmp/meow-artifacts/libbar-3.0.0.pkg.tar.zst"
sha256 = "$lb30"
EOF
    cat > repo/by-name/my/myapp/versions/1.0.0.toml << EOF
[artifact]
filename = "myapp-1.0.0.pkg.tar.zst"
url = "file:///tmp/meow-artifacts/myapp-1.0.0.pkg.tar.zst"
sha256 = "$mya"
EOF
    cat > repo/by-name/my/myapp-exact/versions/1.0.0.toml << EOF
[artifact]
filename = "myapp-exact-1.0.0.pkg.tar.zst"
url = "file:///tmp/meow-artifacts/myapp-exact-1.0.0.pkg.tar.zst"
sha256 = "$mx"
EOF
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


