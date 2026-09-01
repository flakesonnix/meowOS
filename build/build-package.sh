#!/usr/bin/env bash
set -euo pipefail

: "${MEOW_TMPDIR:="/var/tmp/meow"}"
output_dir=""
jobs="$(nproc)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --output|-o) output_dir="$2"; shift 2 ;;
        --jobs|-j) jobs="$2"; shift 2 ;;
        *) break ;;
    esac
done

pkg_dir="${1:-}"
if [ -z "$pkg_dir" ]; then
    echo "usage: $0 [--output <dir>] [--jobs <n>] <pkg-dir>" >&2
    exit 1
fi

pkg_toml="$pkg_dir/package.toml"
if [ ! -f "$pkg_toml" ]; then
    echo "error: $pkg_toml not found" >&2
    exit 1
fi

MEOW_JOBS="$jobs"
export MEOW_JOBS

python3 -c "
import tomllib, sys, os

with open('$pkg_toml', 'rb') as f:
    pkg = tomllib.load(f)

if 'phases' not in pkg:
    sys.exit(0)

meta = pkg.get('source', {})
for k in ('configure', 'build', 'check', 'install'):
    if k in pkg['phases']:
        print(f'--- phase: {k} ---')
        script = pkg['phases'][k]
        path = os.environ.get('MEOW_TMPDIR', '/var/tmp/meow')
        with open(f'{path}/build-phase-{k}.sh', 'w') as f:
            f.write(script)
        os.chmod(f'{path}/build-phase-{k}.sh', 0o755)
" 2>&1

# --- extract metadata ---
src_url=$(python3 -c "
import tomllib
with open('$pkg_toml', 'rb') as f:
    pkg = tomllib.load(f)
print(pkg.get('source', {}).get('url', ''))
")

if [ -z "$src_url" ]; then
    echo "error: no source url in package.toml" >&2
    exit 1
fi

# --- download source ---
src_cache="$MEOW_TMPDIR/source-cache"
mkdir -p "$src_cache"
src_tar="$src_cache/$(basename "$src_url")"

if [ ! -f "$src_tar" ]; then
    echo "downloading $src_url ..."
    curl -sL "$src_url" -o "$src_tar"
fi

# --- set up directories ---
build_root="$MEOW_TMPDIR/build-$$"
src_dir="$build_root/src"
build_dir="$build_root/build"
out_dir="$build_root/out"

mkdir -p "$src_dir" "$build_dir" "$out_dir"

echo "extracting $src_tar ..."
case "$src_tar" in
    *.tar.xz) tar -xJf "$src_tar" -C "$src_dir" --strip-components=1 ;;
    *.tar.gz) tar -xzf "$src_tar" -C "$src_dir" --strip-components=1 ;;
    *.tar.bz2) tar -xjf "$src_tar" -C "$src_dir" --strip-components=1 ;;
    *) tar -xf "$src_tar" -C "$src_dir" --strip-components=1 ;;
esac

src="$src_dir"
build="$build_dir"
out="$out_dir"
export src build out

# --- run phases ---
for phase in configure build check install; do
    script_file="$MEOW_TMPDIR/build-phase-$phase.sh"
    if [ -f "$script_file" ]; then
        echo "--- phase: $phase ---"
        bash -e "$script_file" 2>&1 || true
        rm -f "$script_file"
    fi
done

# --- package with meow-build ---
# Copy all installed files from $out to $pkg_dir/files
# Previous version only copied usr, which misses etc, sbin, lib for OpenRC
if [ -d "$out" ] && [ "$(ls -A "$out" 2>/dev/null)" ]; then
    rm -rf "$pkg_dir/files"
    mkdir -p "$pkg_dir/files"
    cp -a "$out"/* "$pkg_dir/files/" 2>/dev/null || true
    # Handle hidden files if any (e.g. .keep)
    for _f in "$out"/.*; do
        case "$_f" in
            "$out/."|"$out/..") continue ;;
            *) [ -e "$_f" ] && cp -a "$_f" "$pkg_dir/files/" 2>/dev/null || true ;;
        esac
    done
fi

if [ -z "$output_dir" ]; then
    output_dir="$MEOW_TMPDIR/pkg-out"
fi
mkdir -p "$output_dir"

./build/meow-build --output "$output_dir" "$pkg_dir"

rm -rf "$build_root"
