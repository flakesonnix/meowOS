#!/usr/bin/env bash
# Basic install/remove/verify chain (sections 1-10)
set -euo pipefail

run_section() {

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
