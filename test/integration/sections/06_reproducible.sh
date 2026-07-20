#!/usr/bin/env bash
# Reproducible package generation and release check (sections 19, 24)
set -euo pipefail

run_section() {

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
