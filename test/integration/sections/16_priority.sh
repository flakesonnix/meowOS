#!/usr/bin/env bash
# Repository priority selection (section 32)
set -euo pipefail

run_section() {

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
    ./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo "$dir" >/dev/null 2>&1 || true
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
