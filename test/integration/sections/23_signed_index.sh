#!/usr/bin/env bash
# Signed package index (v0.7): manifest/artifact hashes derive their trust from
# packages.toml + packages.toml.sig. Closes HIGH #1 (unsigned per-package
# metadata). Filesystem backend only; no HTTP fixture required.
set -euo pipefail

REPO_SIGN() {
    "$ROOT_DIR/build/meow-repo" sign --key "$KEYS_DIR/meow-release.pem" \
        --key-id meow-release --repo repo >/dev/null 2>&1 || true
}

run_section() {

echo "=== 23. Signed package index ==="

# bootstrapArtifacts already ran `meow-repo sign`, which regenerates and signs
# the index. It must exist and carry every published version.
check "index file generated" "hello" cat repo/packages.toml
check "index signature generated" "signature" cat repo/packages.toml.sig
check "index binds artifact hashes" "artifact_hash" cat repo/packages.toml

echo "=== 23.1 Happy path (index present + valid) ==="
check "list works with signed index" "app" $MEOW --db-path "$TEST_DB" list
check "install verifies against index" "done" $MEOW --db-path "$TEST_DB" install hello
cleanup

echo "=== 23.2 Strict mode accepts a signed index ==="
check "strict mode installs with signed index" "done" \
    env MEOW_REQUIRE_PACKAGE_INDEX=1 $MEOW --db-path "$TEST_DB" install hello
cleanup

# A signed-index trust failure is a non-failover trust error. Like the
# existing repository.toml signature policy, it is surfaced through the mirror
# health status (InvalidMetadata for a manifest/hash mismatch, InvalidSignature
# for a bad/absent index) and the untrusted source is dropped, never served.
echo "=== 23.3 Tampered manifest is rejected (HIGH #1) ==="
# Attacker rewrites the version manifest's artifact hash but cannot re-sign the
# index. The recomputed manifest hash no longer matches the signed entry.
cp repo/by-name/he/hello/versions/1.1.0.toml /tmp/idx-ver-bak
sed -i 's/^sha256 = .*/sha256 = "deadbeef"/' repo/by-name/he/hello/versions/1.1.0.toml
check "tampered manifest rejected (non-failover trust error)" "InvalidMetadata" \
    $MEOW --db-path "$TEST_DB" list
check "tampered manifest package not served" "package not found" \
    $MEOW --db-path "$TEST_DB" install hello
cp /tmp/idx-ver-bak repo/by-name/he/hello/versions/1.1.0.toml
rm -f /tmp/idx-ver-bak

echo "=== 23.4 Tampered index signature is rejected ==="
cp repo/packages.toml /tmp/idx-bak
echo "# tamper" >> repo/packages.toml
check "tampered index rejected" "InvalidSignature" \
    $MEOW --db-path "$TEST_DB" list
cp /tmp/idx-bak repo/packages.toml
rm -f /tmp/idx-bak

echo "=== 23.5 Missing index under strict mode is a hard error ==="
mv repo/packages.toml /tmp/idx-file-bak
mv repo/packages.toml.sig /tmp/idx-sig-bak
check "strict mode rejects missing index" "InvalidSignature" \
    env MEOW_REQUIRE_PACKAGE_INDEX=1 $MEOW --db-path "$TEST_DB" list
# default (non-strict) mode tolerates a missing index (compatibility)
check "compat mode tolerates missing index" "app" \
    $MEOW --db-path "$TEST_DB" list
mv /tmp/idx-file-bak repo/packages.toml
mv /tmp/idx-sig-bak repo/packages.toml.sig

echo "=== 23.6 Unsigned index warns but continues (compat) ==="
mv repo/packages.toml.sig /tmp/idx-sig-bak2
check "unsigned index tolerated in compat mode" "app" \
    $MEOW --db-path "$TEST_DB" list
check "unsigned index rejected in strict mode" "InvalidSignature" \
    env MEOW_REQUIRE_PACKAGE_INDEX=1 $MEOW --db-path "$TEST_DB" list
mv /tmp/idx-sig-bak2 repo/packages.toml.sig

cleanup

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
