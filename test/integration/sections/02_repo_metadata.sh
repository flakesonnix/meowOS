#!/usr/bin/env bash
# Repository metadata format, keys, expiry, identity (sections 11-14)
set -euo pipefail

run_section() {
    require_tools python3 || return 0

echo "=== 11. Format version rejection ==="
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

cp repo/repository.toml /tmp/repo-toml-bak
cat > repo/repository.toml << 'EOF'
format_version = 99
name = "local"
EOF
check "reject bad repository format" "InvalidMetadata" $MEOW --db-path "$TEST_DB" list
cp /tmp/repo-toml-bak repo/repository.toml
rm -f /tmp/repo-toml-bak

cat > /tmp/meow.lock << 'EOF'
lockfile_version = 99
EOF
ln -sf "$PWD/repo" /tmp/repo
OLDPWD=$PWD
cd /tmp
check "reject bad lockfile format" "unsupported lockfile format" $MEOW --db-path /tmp/lock-test.db install --locked hello
cd "$OLDPWD"
rm -f /tmp/meow.lock /tmp/lock-test.db /tmp/repo

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

cp "$KEYS_DIR/meow-release.pub" /tmp/test-add-key.pub
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
sed -i "s/expires = \".*\"/expires = \"$yesterday\"/" repo/repository.toml
./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
check "reject expired repository" "Expired" $MEOW --db-path "$TEST_DB" list
cp /tmp/repo-toml-bak4 repo/repository.toml
cp /tmp/repo-sig-bak4 repo/repository.toml.sig
rm -f /tmp/repo-toml-bak4 /tmp/repo-sig-bak4

echo "=== 14. Repository identity ==="
check "accept repository_id" "app" $MEOW --db-path "$TEST_DB" list

cp repo/repository.toml /tmp/repo-toml-bak5
cp repo/repository.toml.sig /tmp/repo-sig-bak5
sed -i '/^repository_id = /d' repo/repository.toml
./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
check "reject missing repository_id" "InvalidMetadata" $MEOW --db-path "$TEST_DB" list
cp /tmp/repo-toml-bak5 repo/repository.toml
cp /tmp/repo-sig-bak5 repo/repository.toml.sig
rm -f /tmp/repo-toml-bak5 /tmp/repo-sig-bak5

cp repo/repository.toml /tmp/repo-toml-bak6
cp repo/repository.toml.sig /tmp/repo-sig-bak6
sed -i 's/^repository_id = .*/repository_id = "bad id!"/' repo/repository.toml
./build/meow-repo sign --key "$KEYS_DIR/meow-release.pem" --key-id meow-release --repo repo >/dev/null 2>&1 || true
check "reject invalid repository_id" "InvalidMetadata" $MEOW --db-path "$TEST_DB" list
cp /tmp/repo-toml-bak6 repo/repository.toml
cp /tmp/repo-sig-bak6 repo/repository.toml.sig
rm -f /tmp/repo-toml-bak6 /tmp/repo-sig-bak6

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
