# Common helpers and setup for the integration test suite.
# Sourced by test/integration.sh (not standalone).

# Resolve the script directory reliably whether this file is sourced by
# test/integration.sh ($0 points at the wrapper) or run standalone
# (BASH_SOURCE is the empty string). $(dirname "$0") is wrong in the
# sourced case, so derive everything from BASH_SOURCE[0] instead.
_COMMON_SRC="${BASH_SOURCE[0]:-$0}"
SCRIPT_DIR="$(cd "$(dirname "$_COMMON_SRC")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

MEOW="$ROOT_DIR/build/meow"
KEYS_DIR="$ROOT_DIR/test/keys"
: "${TMPDIR:=/tmp}"

# ---------------------------------------------------------------------------
# Fixture isolation
#
# Every helper below returns an isolated directory under a single run-scoped
# root and registers it for automatic cleanup. Isolating repo / home / cache /
# database state is what makes the suite safe to run in parallel later: no
# section shares the canonical ./repo, the user's ~/.cache/meow, or
# ~/.config/meow/keys with another section.
# ---------------------------------------------------------------------------

FIXTURE_ROOT="$TMPDIR/meow-fixtures-$$"
FIXTURE_SEQ=0

# Create the run-scoped fixture root and register an exit trap so every
# fixture directory is removed automatically (no manual git clean required).
init_fixtures() {
    mkdir -p "$FIXTURE_ROOT"
    trap cleanup_fixtures EXIT INT TERM
}

# Remove all fixtures created for this run. Idempotent and safe to call twice.
cleanup_fixtures() {
    rm -rf "$FIXTURE_ROOT"
}

# Copy the canonical ./repo into a private workdir that also contains a `repo`
# entry, and print that workdir's path. Callers `cd` into the result so that
# `./repo` resolves to the copy while absolute paths ($MEOW, keys,
# http_fixture.py) keep working. This is the recommended replacement for
# mutating the shared ./repo or for `cp -r repo repo-dual`.
#
# The copy method is selected by FIXTURE_COPY (default "cp -a"): see the
# benchmark notes in the fixture audit. Supported: "cp -a", "rsync -a",
# "cp -al" (hardlinks, where the filesystem allows them).
create_repo_fixture() {
    FIXTURE_SEQ=$((FIXTURE_SEQ + 1))
    local work="$FIXTURE_ROOT/repo-work-$FIXTURE_SEQ"
    rm -rf "$work"
    mkdir -p "$work"
    _copy_repo repo "$work/repo"
    echo "$work"
}

# Copy the canonical ./repo into an explicit target directory (used by
# bootstrapArtifacts when an isolated repo root is active). Honors FIXTURE_COPY.
_copy_repo() {
    local src="$1" dst="$2"
    case "${FIXTURE_COPY:-cp -a}" in
        rsync\ -a) rsync -a "$src/" "$dst/" ;;
        cp\ -al)  cp -al "$src" "$dst" 2>/dev/null || cp -a "$src" "$dst" ;;
        *)        cp -a "$src" "$dst" ;;
    esac
}

# Build an isolated HOME directory containing a copy of the trusted keys and an
# empty cache. Exporting HOME to the result isolates both ~/.cache/meow and
# ~/.config/meow/keys for the remainder of the run. The trust store expects the
# PUBLIC key, so the .pub file is installed as the trusted key.
create_home_fixture() {
    FIXTURE_SEQ=$((FIXTURE_SEQ + 1))
    local home="$FIXTURE_ROOT/home-$FIXTURE_SEQ"
    mkdir -p "$home/.config/meow/keys" "$home/.cache/meow"
    cp "$KEYS_DIR/meow-release.pub" "$home/.config/meow/keys/meow-release.pem" 2>/dev/null || true
    echo "$home"
}

# Return an isolated keys directory containing a copy of the trusted PUBLIC key.
# Use this when a section needs to mutate ~/.config/meow/keys without adopting a
# full isolated HOME (e.g. to drop or re-add a trusted key).
create_keys_fixture() {
    FIXTURE_SEQ=$((FIXTURE_SEQ + 1))
    local keys="$FIXTURE_ROOT/keys-$FIXTURE_SEQ"
    mkdir -p "$keys"
    cp "$KEYS_DIR/meow-release.pub" "$keys/meow-release.pem" 2>/dev/null || true
    echo "$keys"
}

# Return an isolated keys directory containing a copy of the trusted keys.
# Use this when a section needs to mutate ~/.config/meow/keys without adopting a
# full isolated HOME (e.g. to drop or re-add a trusted key).
create_keys_fixture() {
    FIXTURE_SEQ=$((FIXTURE_SEQ + 1))
    local keys="$FIXTURE_ROOT/keys-$FIXTURE_SEQ"
    mkdir -p "$keys"
    # meow loads verification keys by matching *.pem, so copy the
    # public key under a .pem name.
    cp "$KEYS_DIR/meow-release.pub" "$keys/meow-release.pem" 2>/dev/null || true
    echo "$keys"
}

# Return an isolated cache directory (used when a section needs its own
# ~/.cache/meow without adopting a full isolated HOME).
create_cache_fixture() {
    FIXTURE_SEQ=$((FIXTURE_SEQ + 1))
    local cache="$FIXTURE_ROOT/cache-$FIXTURE_SEQ"
    mkdir -p "$cache"
    echo "$cache"
}

# Return a unique, empty database path for a section. Replaces the shared
# TEST_DB so concurrent sections never collide on the same file.
create_db_fixture() {
    FIXTURE_SEQ=$((FIXTURE_SEQ + 1))
    mktemp -p "$FIXTURE_ROOT" "db-$FIXTURE_SEQ.XXXXXX"
}

# Default database for sections that still reference $TEST_DB. Kept unique per
# run; individual sections may call create_db_fixture() for stronger isolation.
TEST_DB="$(mktemp -p "$TMPDIR" "meow-test-$$.XXXXXX")"
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
    "$ROOT_DIR/build/meow-build" --output /tmp/meow-artifacts "$src" >/dev/null 2>&1
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
    "$ROOT_DIR/build/meow-repo" sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
}

reproBuild() {
    local src="$1" out="$2" base="$3"; shift 3
    rm -rf "$out"
    mkdir -p "$out"
    env "$@" "$ROOT_DIR/build/meow-build" --output "$out" "$src" >/dev/null 2>&1
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
    "$ROOT_DIR/build/meow-build" --output /tmp/meow-artifacts "$src" >/dev/null 2>&1
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
    "$ROOT_DIR/build/meow-repo" sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
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
    "$ROOT_DIR/build/meow-repo" sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
}

HTTP_PID=""
HTTP_PORT=""
startHttp() {
    python3 "$SCRIPT_DIR/http_fixture.py" --root /tmp/meow-http-root --port 0 >/tmp/meow-http.log 2>&1 &
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


