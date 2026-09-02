#!/usr/bin/env bash
# CLI non-interactive mode (APT-like --yes) - regression for Gate C
# Ensures meow install/remove accept --yes/-y without treating it as a package name.
set -euo pipefail

run_section() {

echo "=== 25. CLI non-interactive (--yes) ==="

# Use the shared test DB from the harness (already bootstrapped via bootstrapArtifacts)
# Test --yes as a no-op flag for install/remove (APT-like UX)

check "install neofetch --yes (after)" "Transaction committed" $MEOW --db-path "$TEST_DB" install neofetch --yes 2>&1 | tail -n 5
check "neofetch installed after --yes" "neofetch 7.1.0" $MEOW --db-path "$TEST_DB" installed 2>&1 | head -n 20

check "remove neofetch --yes" "bash" bash -c "$MEOW --db-path \"$TEST_DB\" remove neofetch --yes 2>&1; $MEOW --db-path \"$TEST_DB\" installed 2>&1 | head -n 20"
check "neofetch removed after --yes" "neofetch" bash -c "! $MEOW --db-path \"$TEST_DB\" installed 2>&1 | grep -q 'neofetch 7.1.0' && echo 'neofetch removed'"

check "install --yes neofetch (before)" "Transaction committed" $MEOW --db-path "$TEST_DB" install --yes neofetch 2>&1 | tail -n 5
check "neofetch reinstalled with --yes before" "neofetch 7.1.0" $MEOW --db-path "$TEST_DB" installed 2>&1 | head -n 20

check "remove second time" "bash" bash -c "$MEOW --db-path \"$TEST_DB\" remove neofetch 2>&1; $MEOW --db-path \"$TEST_DB\" installed 2>&1 | head -n 20"
check "neofetch removed second time" "neofetch" bash -c "! $MEOW --db-path \"$TEST_DB\" installed 2>&1 | grep -q 'neofetch 7.1.0' && echo 'neofetch removed'"
check "install neofetch -y (short)" "Transaction committed" $MEOW --db-path "$TEST_DB" install neofetch -y 2>&1 | tail -n 5
check "neofetch installed with -y" "neofetch 7.1.0" $MEOW --db-path "$TEST_DB" installed 2>&1 | head -n 20

# Verify via meow info that the package is correctly recorded
check "neofetch info after --yes" "neofetch" $MEOW --db-path "$TEST_DB" info neofetch 2>&1 | head -n 20

# Also verify --yes does not create a spurious package entry
check "no spurious --yes package" "no spurious" bash -c "! $MEOW --db-path \"$TEST_DB\" info -- --yes 2>&1 | grep -qF -- \"--yes\" && echo 'no spurious --yes package'"

cleanup

}
