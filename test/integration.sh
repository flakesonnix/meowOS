#!/usr/bin/env bash
# Integration test suite for meow package manager
# Run from repo root: nix develop --command ./test/integration.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

. "$SCRIPT_DIR/integration/common.sh"

init_fixtures
export HOME="$(create_home_fixture)"

cleanup
bootstrapArtifacts

# Source and run each section in the original module-execution order
# NB: common.sh overwrites SCRIPT_DIR with the directory of common.sh
# itself (${BASH_SOURCE[0]}), so recompute section-less paths here.
INTEGRATION_DIR="$ROOT_DIR/test"
SECTION_DIR="$INTEGRATION_DIR/integration/sections"

run_section_from() {
  . "$SECTION_DIR/$1"
  run_section
}

# -- install.sh sections (1-10, 15-17, 22-23) --
run_section_from "01_basic_install.sh"
run_section_from "03_download.sh"
run_section_from "04_http.sh"
run_section_from "08_fresh_install.sh"

# -- repository.sh sections (11-14, 25-29, 31-35) --
run_section_from "02_repo_metadata.sh"
run_section_from "09_server_hosting.sh"
run_section_from "10_http_backend.sh"
run_section_from "11_multi_repo.sh"
run_section_from "12_doctor_repo.sh"
run_section_from "13_dual_backend.sh"
run_section_from "15_health_state.sh"
run_section_from "16_priority.sh"
run_section_from "17_mirror_groups.sh"
run_section_from "18_mirror_failover.sh"
run_section_from "19_parallel_refresh.sh"

# -- package_features.sh sections (18-21, 24, groups, 36-37) --
run_section_from "05_doctor.sh"
run_section_from "06_reproducible.sh"
run_section_from "07_hooks.sh"
run_section_from "22_groups.sh"
run_section_from "20_history.sh"
run_section_from "21_optionals.sh"

# -- resolver.sh sections (30-31-31-32) --
run_section_from "14_resolver.sh"

echo ""
echo "Results: $pass passed, $fail failed, $skip skipped"
git checkout -- repo 2>/dev/null || true
git clean -fdq repo 2>/dev/null || true
[ "$fail" -eq 0 ] || exit 1
