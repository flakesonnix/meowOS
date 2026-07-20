#!/usr/bin/env bash
# Restricted hook runner (section 20)
set -euo pipefail

run_section() {

echo "=== 20. Restricted hook runner ==="
HOOK_OK="/tmp/meow-hook-ok"
rm -rf "$HOOK_OK"
mkdir -p "$HOOK_OK"
cat > "$HOOK_OK/post_install" << 'EOF'
#!/bin/sh
echo "PKG=$MEOW_PACKAGE VER=$MEOW_VERSION TYPE=$MEOW_HOOK_TYPE"
echo "PATH=$PATH"
echo "CWD=$(pwd)"
env | while IFS= read -r line; do
    name=${line%%=*}
    echo "VAR:$name"
done
EOF
chmod +x "$HOOK_OK/post_install"
registerHookPkg hookok 1.0.0 "$HOOK_OK"
rm -rf /tmp/meow-hook-install
out=$(MEOW_HOOK_TIMEOUT=30 $MEOW --db-path "$TEST_DB" install hookok 2>&1)
if echo "$out" | grep -q "done"; then
    echo "  PASS: successful hook install completes"
    pass=$((pass + 1))
else
    echo "  FAIL: successful hook install failed"
    fail=$((fail + 1))
fi
REQUIRED="HOME PATH TMPDIR MEOW_PACKAGE MEOW_VERSION MEOW_HOOK_TYPE MEOW_HOOK_STAGING"
abi_ok=yes
for v in $REQUIRED; do
    echo "$out" | grep -q "VAR:$v" || abi_ok=no
done
no_leak=yes
for pat in CI GITHUB_ NIX_ SSH_ XDG_; do
    echo "$out" | grep -q "VAR:$pat" && no_leak=no
done
if echo "$out" | grep -Eq "VAR:(CI|GITHUB_|NIX_|SSH_|XDG_)"; then
    no_leak=no
fi
if echo "$out" | grep -q "PKG=hookok VER=1.0.0 TYPE=post_install" && \
   echo "$out" | grep -q "PATH=/usr/bin:/bin" && \
   echo "$out" | grep -q "CWD=.*meow/hooks/hookok/post_install" && \
   [ "$abi_ok" = "yes" ] && [ "$no_leak" = "yes" ]; then
    echo "  PASS: hook runs with isolated cwd + minimal env (required present, no leaks)"
    pass=$((pass + 1))
else
    echo "  FAIL: hook environment not isolated"
    echo "    required_ok=$abi_ok leak_ok=$no_leak"
    echo "    missing: $(for v in $REQUIRED; do echo "$out" | grep -q "VAR:$v" || echo -n "$v "; done)"
    echo "    leaked: $(echo "$out" | grep -oE 'VAR:(CI|GITHUB_|NIX_|SSH_|XDG_)[A-Za-z0-9_]*' | tr '\n' ' ')"
    fail=$((fail + 1))
fi
if echo "$out" | grep -q "post_install output:"; then
    echo "  PASS: hook output captured"
    pass=$((pass + 1))
else
    echo "  FAIL: hook output not captured"
    fail=$((fail + 1))
fi
$MEOW --db-path "$TEST_DB" remove hookok >/dev/null 2>&1 || true

HOOK_BAD="/tmp/meow-hook-bad"
rm -rf "$HOOK_BAD"
mkdir -p "$HOOK_BAD"
cat > "$HOOK_BAD/post_install" << 'EOF'
#!/bin/sh
echo "boom"
exit 3
EOF
chmod +x "$HOOK_BAD/post_install"
registerHookPkg hookbad 1.0.0 "$HOOK_BAD"
rm -rf /tmp/meow-hook-install
out=$($MEOW --db-path "$TEST_DB" install hookbad 2>&1 || true)
if echo "$out" | grep -q "HookFailed"; then
    echo "  PASS: failing hook reported as HookFailed"
    pass=$((pass + 1))
else
    echo "  FAIL: failing hook not reported"
    fail=$((fail + 1))
fi
if ! $MEOW --db-path "$TEST_DB" installed 2>/dev/null | grep -q "hookbad"; then
    echo "  PASS: failed hook rolled back install"
    pass=$((pass + 1))
else
    echo "  FAIL: hookbad present after failed hook"
    fail=$((fail + 1))
fi
$MEOW --db-path "$TEST_DB" remove hookbad >/dev/null 2>&1 || true

HOOK_SLOW="/tmp/meow-hook-slow"
rm -rf "$HOOK_SLOW"
mkdir -p "$HOOK_SLOW"
cat > "$HOOK_SLOW/post_install" << 'EOF'
#!/bin/sh
while true; do :; done
EOF
chmod +x "$HOOK_SLOW/post_install"
registerHookPkg hookslow 1.0.0 "$HOOK_SLOW"
rm -rf /tmp/meow-hook-install
out=$(MEOW_HOOK_TIMEOUT=1 $MEOW --db-path "$TEST_DB" install hookslow 2>&1 || true)
if echo "$out" | grep -q "HookTimeout"; then
    echo "  PASS: runaway hook killed by timeout"
    pass=$((pass + 1))
else
    echo "  FAIL: runaway hook not timed out"
    fail=$((fail + 1))
fi
$MEOW --db-path "$TEST_DB" remove hookslow >/dev/null 2>&1 || true

rm -rf "$HOOK_OK" "$HOOK_BAD" "$HOOK_SLOW" /tmp/meow-hook-src-*

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
