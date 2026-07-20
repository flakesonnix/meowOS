#!/usr/bin/env bash
# Repository health state (section 31)
set -euo pipefail

run_section() {

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
./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo-expired >/dev/null 2>&1 || true

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
./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo-badsig >/dev/null 2>&1 || true
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
