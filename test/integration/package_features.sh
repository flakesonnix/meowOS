# Package features: doctor, reproducibility, hooks, security, groups, history, optionals
# Sections 18-21, 24, groups, 36-37
run_package_features_sections() {

echo "=== 18. doctor diagnostics ==="
check "doctor runs" "meow doctor:" $MEOW --db-path "$TEST_DB" doctor
check "doctor --json has checks" '"checks": [' $MEOW --db-path "$TEST_DB" doctor --json

echo ""
echo "=== 19. Reproducible package generation ==="
REPRO_SRC="/tmp/meow-repro-src"
rm -rf "$REPRO_SRC"
mkdir -p "$REPRO_SRC/files/usr/bin" "$REPRO_SRC/files/etc"
printf "#!/bin/sh\necho hi\n" > "$REPRO_SRC/files/usr/bin/hello"
chmod +x "$REPRO_SRC/files/usr/bin/hello"
echo "config" > "$REPRO_SRC/files/etc/hello.conf"
cat > "$REPRO_SRC/package.toml" << 'EOF'
name = "hello"
version = "1.0.0"
architecture = "AMD64"
description = "reproducibility fixture"

[build]
reproducible = true
source_date_epoch = 1700000000
EOF

h_a=$(reproBuild "$REPRO_SRC" /tmp/meow-repro-a hello-1.0.0.pkg.tar.zst)
h_b=$(reproBuild "$REPRO_SRC" /tmp/meow-repro-b hello-1.0.0.pkg.tar.zst)
if [ "$h_a" = "$h_b" ] && [ -n "$h_a" ]; then
    echo "  PASS: same source builds identical artifact"
    pass=$((pass + 1))
else
    echo "  FAIL: repeated build differs ($h_a vs $h_b)"
    fail=$((fail + 1))
fi

touch -d "1999-01-01 00:00:00" "$REPRO_SRC/files/usr/bin/hello" "$REPRO_SRC/files/etc/hello.conf"
h_m=$(reproBuild "$REPRO_SRC" /tmp/meow-repro-m hello-1.0.0.pkg.tar.zst)
if [ "$h_m" = "$h_a" ]; then
    echo "  PASS: source mtime does not affect artifact"
    pass=$((pass + 1))
else
    echo "  FAIL: mtime changed artifact ($h_m vs $h_a)"
    fail=$((fail + 1))
fi

rm -rf "$REPRO_SRC/files"
mkdir -p "$REPRO_SRC/files/etc" "$REPRO_SRC/files/usr/bin"
echo "config" > "$REPRO_SRC/files/etc/hello.conf"
printf "#!/bin/sh\necho hi\n" > "$REPRO_SRC/files/usr/bin/hello"
chmod +x "$REPRO_SRC/files/usr/bin/hello"
h_o=$(reproBuild "$REPRO_SRC" /tmp/meow-repro-o hello-1.0.0.pkg.tar.zst)
if [ "$h_o" = "$h_a" ]; then
    echo "  PASS: source file order does not affect artifact"
    pass=$((pass + 1))
else
    echo "  FAIL: file order changed artifact ($h_o vs $h_a)"
    fail=$((fail + 1))
fi

REPRO_ENV="/tmp/meow-repro-env"
rm -rf "$REPRO_ENV"
mkdir -p "$REPRO_ENV/files/usr/bin"
printf "#!/bin/sh\necho hi\n" > "$REPRO_ENV/files/usr/bin/hello"
chmod +x "$REPRO_ENV/files/usr/bin/hello"
cat > "$REPRO_ENV/package.toml" << 'EOF'
name = "hello"
version = "1.0.0"
architecture = "AMD64"
description = "reproducibility fixture"
EOF
h_default=$(reproBuild "$REPRO_ENV" /tmp/meow-repro-d hello-1.0.0.pkg.tar.zst)
h_e=$(reproBuild "$REPRO_ENV" /tmp/meow-repro-e hello-1.0.0.pkg.tar.zst SOURCE_DATE_EPOCH=1720000000)
if [ -n "$h_e" ] && [ -n "$h_default" ] && [ "$h_e" != "$h_default" ]; then
    echo "  PASS: SOURCE_DATE_EPOCH override changes hash deterministically"
    pass=$((pass + 1))
else
    echo "  FAIL: SOURCE_DATE_EPOCH had no effect ($h_e vs $h_default)"
    fail=$((fail + 1))
fi
rm -rf "$REPRO_ENV" /tmp/meow-repro-d

if ./build/meow-build --output /tmp/meow-repro-x "$REPRO_SRC" >/dev/null 2>&1 && \
   tar --zstd -tf /tmp/meow-repro-x/hello-1.0.0.pkg.tar.zst 2>/dev/null | grep -q "metadata/build.json"; then
    echo "  PASS: metadata/build.json embedded in archive"
    pass=$((pass + 1))
else
    echo "  FAIL: metadata/build.json missing from archive"
    fail=$((fail + 1))
fi

rm -rf "$REPRO_SRC" /tmp/meow-repro-a /tmp/meow-repro-b /tmp/meow-repro-m \
       /tmp/meow-repro-o /tmp/meow-repro-e /tmp/meow-repro-x

echo ""
echo "=== 20. Restricted hook runner ==="
KEY="$(dirname "$0")/keys/meow-release.pem"

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

echo ""
echo "=== 21. doctor --security ==="
sec=$(MEOW_HOOK_TIMEOUT=30 $MEOW --db-path "$TEST_DB" doctor --security 2>&1)
for needle in "trusted keys configured" "signature valid" "metadata not expired" "no stale partial downloads" "hook policy loaded"; do
    if echo "$sec" | grep -q "$needle"; then
        echo "  PASS: security check present: $needle"
        pass=$((pass + 1))
    else
        echo "  FAIL: missing security check: $needle"
        fail=$((fail + 1))
    fi
done
if [ -d "$HOME/.cache/meow" ]; then
    touch "$HOME/.cache/meow/stale-$$.pkg.tar.zst.part"
    sec2=$($MEOW --db-path "$TEST_DB" doctor --security 2>&1)
    if echo "$sec2" | grep -q "stale .part"; then
        echo "  PASS: stale partial download detected"
        pass=$((pass + 1))
    else
        echo "  FAIL: stale partial download not detected"
        fail=$((fail + 1))
    fi
    rm -f "$HOME/.cache/meow/stale-$$.pkg.tar.zst.part"
fi

echo ""
echo "=== 24. reproducibility release check ==="
REL_SRC="/tmp/meow-rel-src"
rm -rf "$REL_SRC"
mkdir -p "$REL_SRC/files/usr/bin"
printf "#!/bin/sh\necho hi\n" > "$REL_SRC/files/usr/bin/hello"
chmod +x "$REL_SRC/files/usr/bin/hello"
cat > "$REL_SRC/package.toml" << 'EOF'
name = "hello"
version = "1.0.0"
architecture = "AMD64"
description = "reproducibility release fixture"

[build]
reproducible = true
source_date_epoch = 1700000000
EOF
h1=$(reproBuild "$REL_SRC" /tmp/meow-rel-a hello-1.0.0.pkg.tar.zst || true)
h2=$(reproBuild "$REL_SRC" /tmp/meow-rel-b hello-1.0.0.pkg.tar.zst || true)
if [ -n "$h1" ] && [ "$h1" = "$h2" ]; then
    echo "  PASS: reproducible build is byte-identical ($h1)"
    pass=$((pass + 1))
else
    echo "  FAIL: reproducible build differs ($h1 vs $h2)"
    fail=$((fail + 1))
fi
rm -rf "$REL_SRC"

echo ""
echo "=== package groups ==="
cat > meow-groups.toml << EOF
[[repositories]]
id = "main"
url = "./repo"
priority = 100

[[groups]]
name = "base-devel"
packages = ["hello", "libfoo"]

[[groups]]
name = "with-deps"
packages = ["app"]
EOF

GRP_DB="/tmp/meow-grp-$$.db"

if $MEOW --db-path "$GRP_DB" --config meow-groups.toml group list 2>/dev/null | grep -q "base-devel"; then
    echo "  PASS: parse group config"
    pass=$((pass + 1))
else
    echo "  FAIL: group config not parsed"
    fail=$((fail + 1))
fi
if $MEOW --db-path "$GRP_DB" --config meow-groups.toml group list 2>/dev/null | grep -q "hello" && \
   $MEOW --db-path "$GRP_DB" --config meow-groups.toml group list 2>/dev/null | grep -q "libfoo"; then
    echo "  PASS: list groups shows members"
    pass=$((pass + 1))
else
    echo "  FAIL: group members not listed"
    fail=$((fail + 1))
fi

if $MEOW --db-path "$GRP_DB" --config meow-groups.toml group install base-devel >/dev/null 2>&1; then
    OUT=$($MEOW --db-path "$GRP_DB" installed 2>/dev/null)
    if echo "$OUT" | grep -q "^hello" && echo "$OUT" | grep -q "^libfoo"; then
        echo "  PASS: install group installs all members"
        pass=$((pass + 1))
    else
        echo "  FAIL: group members not all installed"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: group install failed"
    fail=$((fail + 1))
fi

GRP_DB2="/tmp/meow-grp2-$$.db"
if $MEOW --db-path "$GRP_DB2" --config meow-groups.toml group install with-deps >/dev/null 2>&1; then
    OUT2=$($MEOW --db-path "$GRP_DB2" installed 2>/dev/null)
    if echo "$OUT2" | grep -q "^app" && echo "$OUT2" | grep -q "^hello" && echo "$OUT2" | grep -q "^libfoo"; then
        echo "  PASS: group install resolves dependency closure"
        pass=$((pass + 1))
    else
        echo "  FAIL: group dependency closure incomplete"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: group with-deps install failed"
    fail=$((fail + 1))
fi

GRP_DB3="/tmp/meow-grp3-$$.db"
cat > meow-groups-bad.toml << EOF
[[repositories]]
id = "main"
url = "./repo"
priority = 100

[[groups]]
name = "broken"
packages = ["hello", "no-such-package"]
EOF
$MEOW --db-path "$GRP_DB3" --config meow-groups-bad.toml group install broken >/dev/null 2>&1 || true
if $MEOW --db-path "$GRP_DB3" installed 2>/dev/null | grep -q "^hello"; then
    echo "  FAIL: partial group install left packages behind"
    fail=$((fail + 1))
else
    echo "  PASS: group install is atomic (nothing installed on failure)"
    pass=$((pass + 1))
fi

if OUT=$($MEOW --db-path "$GRP_DB" --config meow-groups.toml group install nosuchgroup 2>&1); echo "$OUT" | grep -qi "group not found"; then
    echo "  PASS: missing group rejected"
    pass=$((pass + 1))
else
    echo "  FAIL: missing group not rejected"
    fail=$((fail + 1))
fi

cat > meow-groups-dup.toml << EOF
[[repositories]]
id = "main"
url = "./repo"
priority = 100

[[groups]]
name = "dup"
packages = ["hello"]

[[groups]]
name = "dup"
packages = ["libfoo"]
EOF
if OUT=$($MEOW --db-path "$GRP_DB" --config meow-groups-dup.toml list 2>&1); echo "$OUT" | grep -qi "duplicate group"; then
    echo "  PASS: duplicate group rejected"
    pass=$((pass + 1))
else
    echo "  FAIL: duplicate group not rejected"
    fail=$((fail + 1))
fi

cat > meow-groups-rsv.toml << EOF
[[repositories]]
id = "main"
url = "./repo"
priority = 100

[[groups]]
name = "install"
packages = ["hello"]
EOF
if OUT=$($MEOW --db-path "$GRP_DB" --config meow-groups-rsv.toml list 2>&1); echo "$OUT" | grep -qi "reserved command"; then
    echo "  PASS: reserved-name group rejected"
    pass=$((pass + 1))
else
    echo "  FAIL: reserved-name group not rejected"
    fail=$((fail + 1))
fi

rm -f meow-groups.toml meow-groups-bad.toml meow-groups-dup.toml meow-groups-rsv.toml

echo ""
echo "=== 36. Package history ==="
cat > meow-hist.toml << EOF
[[repositories]]
id = "main"
url = "./repo"
priority = 100

[[groups]]
name = "base-devel"
packages = ["hello", "libfoo"]
EOF

HDB="/tmp/meow-hist-$$.db"

if $MEOW --db-path "$HDB" --config meow-hist.toml install hello >/dev/null 2>&1; then
    if OUT=$($MEOW --db-path "$HDB" why hello 2>/dev/null); echo "$OUT" | grep -qi "reason: explicit"; then
        echo "  PASS: explicit install records reason"
        pass=$((pass + 1))
    else
        echo "  FAIL: explicit reason not recorded"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: explicit install failed"
    fail=$((fail + 1))
fi

if $MEOW --db-path "$HDB" --config meow-hist.toml group install base-devel >/dev/null 2>&1; then
    if OUT=$($MEOW --db-path "$HDB" why libfoo 2>/dev/null); echo "$OUT" | grep -qi "reason: group"; then
        echo "  PASS: group install records group member"
        pass=$((pass + 1))
    else
        echo "  FAIL: group member reason not recorded"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: group install failed"
    fail=$((fail + 1))
fi

if $MEOW --db-path "$HDB" --config meow-hist.toml group install base-devel >/dev/null 2>&1; then
    if OUT=$($MEOW --db-path "$HDB" why hello 2>/dev/null); echo "$OUT" | grep -qi "reason: explicit"; then
        echo "  PASS: explicit reason survives later group install"
        pass=$((pass + 1))
    else
        echo "  FAIL: explicit reason downgraded by group install"
        fail=$((fail + 1))
    fi
fi

HDB2="/tmp/meow-hist2-$$.db"
if $MEOW --db-path "$HDB2" --config meow-hist.toml install app >/dev/null 2>&1; then
    OUT=$($MEOW --db-path "$HDB2" installed 2>/dev/null)
    if echo "$OUT" | grep -q "^app"; then
        if WOUT=$($MEOW --db-path "$HDB2" why libfoo 2>/dev/null); echo "$WOUT" | grep -qi "reason: dependency"; then
            echo "  PASS: dependency install records dependency"
            pass=$((pass + 1))
        else
            echo "  FAIL: dependency reason not recorded"
            fail=$((fail + 1))
        fi
    else
        echo "  FAIL: app install did not resolve"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: dependency install failed"
    fail=$((fail + 1))
fi

if OUT=$($MEOW --db-path "$HDB" history 2>/dev/null); echo "$OUT" | grep -qi "install hello"; then
    echo "  PASS: history lists install actions"
    pass=$((pass + 1))
else
    echo "  FAIL: history missing install actions"
    fail=$((fail + 1))
fi

if OUT=$($MEOW --db-path "$HDB" history hello 2>/dev/null); echo "$OUT" | grep -q "hello" && ! echo "$OUT" | grep -q "libfoo"; then
    echo "  PASS: history <package> restricts to package"
    pass=$((pass + 1))
else
    echo "  FAIL: history <package> not scoped"
    fail=$((fail + 1))
fi

if $MEOW --db-path "$HDB" remove libfoo >/dev/null 2>&1; then
    if OUT=$($MEOW --db-path "$HDB" history libfoo 2>/dev/null); echo "$OUT" | grep -qi "remove libfoo"; then
        echo "  PASS: remove records history"
        pass=$((pass + 1))
    else
        echo "  FAIL: remove not in history"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: remove failed"
    fail=$((fail + 1))
fi

if OUT=$($MEOW --db-path "$HDB" explicitly-installed 2>/dev/null); \
   echo "$OUT" | grep -q "^hello" && ! echo "$OUT" | grep -q "libfoo"; then
    echo "  PASS: explicitly-installed lists explicit only"
    pass=$((pass + 1))
else
    echo "  FAIL: explicitly-installed incorrect"
    fail=$((fail + 1))
fi

rm -f meow-hist.toml

echo ""
echo "=== 37. Optional dependency installation ==="
registerOptPkg gtk4 1.0.0 usr/bin/gtk4
registerOptPkg qt6 1.0.0 usr/bin/qt6

cat > meow-opt.toml << EOF
[[repositories]]
id = "main"
url = "./repo"
priority = 100
EOF

ODB="/tmp/meow-opt-$$.db"

if $MEOW --db-path "$ODB" --config meow-opt.toml install app >/dev/null 2>&1; then
    if OUT=$($MEOW --db-path "$ODB" installed 2>/dev/null); \
       echo "$OUT" | grep -q "^app" && ! echo "$OUT" | grep -q "gtk4" && ! echo "$OUT" | grep -q "qt6"; then
        echo "  PASS: metadata only doesn't install optional"
        pass=$((pass + 1))
    else
        echo "  FAIL: optional installed without flag"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: app install failed"
    fail=$((fail + 1))
fi

ODB2="/tmp/meow-opt2-$$.db"
if $MEOW --db-path "$ODB2" --config meow-opt.toml install app --with-optional >/dev/null 2>&1; then
    if OUT=$($MEOW --db-path "$ODB2" installed 2>/dev/null); \
       echo "$OUT" | grep -q "^gtk4" && echo "$OUT" | grep -q "^qt6"; then
        echo "  PASS: --with-optional installs all optional"
        pass=$((pass + 1))
    else
        echo "  FAIL: --with-optional did not install optionals"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: --with-optional install failed"
    fail=$((fail + 1))
fi

ODB3="/tmp/meow-opt3-$$.db"
if $MEOW --db-path "$ODB3" --config meow-opt.toml install app --optional gtk4 >/dev/null 2>&1; then
    if OUT=$($MEOW --db-path "$ODB3" installed 2>/dev/null); \
       echo "$OUT" | grep -q "^gtk4" && ! echo "$OUT" | grep -q "qt6"; then
        echo "  PASS: --optional installs selected package"
        pass=$((pass + 1))
    else
        echo "  FAIL: --optional did not install only selected"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: --optional install failed"
    fail=$((fail + 1))
fi

ODB4="/tmp/meow-opt4-$$.db"
if $MEOW --db-path "$ODB4" --config meow-opt.toml install app --optional gtk4 --optional qt6 >/dev/null 2>&1; then
    if OUT=$($MEOW --db-path "$ODB4" installed 2>/dev/null); \
       echo "$OUT" | grep -q "^gtk4" && echo "$OUT" | grep -q "^qt6"; then
        echo "  PASS: multiple --optional flags"
        pass=$((pass + 1))
    else
        echo "  FAIL: multiple --optional did not install all"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: multiple --optional install failed"
    fail=$((fail + 1))
fi

if OUT=$($MEOW --db-path "$ODB" --config meow-opt.toml install app --optional nonexistent 2>&1); \
   echo "$OUT" | grep -qi "not an optional dependency"; then
    echo "  PASS: invalid optional rejected"
    pass=$((pass + 1))
else
    echo "  FAIL: invalid optional not rejected"
    fail=$((fail + 1))
fi

if OUT=$($MEOW --db-path "$ODB2" why gtk4 2>/dev/null); echo "$OUT" | grep -qi "reason: explicit"; then
    echo "  PASS: optional recorded as Explicit"
    pass=$((pass + 1))
else
    echo "  FAIL: optional not recorded as Explicit"
    fail=$((fail + 1))
fi

if OUT=$($MEOW --db-path "$ODB2" history gtk4 2>/dev/null); echo "$OUT" | grep -qi "install gtk4"; then
    echo "  PASS: history preserved"
    pass=$((pass + 1))
else
    echo "  FAIL: history missing optional install"
    fail=$((fail + 1))
fi

rm -f meow-opt.toml

}
