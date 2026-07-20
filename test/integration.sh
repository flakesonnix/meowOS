#!/usr/bin/env bash
# Integration test suite for meow package manager
# Run from repo root: nix develop --command ./test/integration.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Source all module files
. "$SCRIPT_DIR/integration/common.sh"
. "$SCRIPT_DIR/integration/install.sh"
. "$SCRIPT_DIR/integration/repository.sh"
. "$SCRIPT_DIR/integration/package_features.sh"
. "$SCRIPT_DIR/integration/resolver.sh"

# Install trusted key for repo verification
mkdir -p ~/.config/meow/keys
cp "$SCRIPT_DIR/keys/meow-release.pub" ~/.config/meow/keys/meow-release.pem

cleanup
bootstrapArtifacts

# Run sections in the original numbered order
run_install_sections
run_repository_sections
run_package_features_sections
run_resolver_sections

echo ""
echo "Results: $pass passed, $fail failed"
git checkout -- repo 2>/dev/null || true
git clean -fdq repo 2>/dev/null || true
[ "$fail" -eq 0 ] || exit 1
