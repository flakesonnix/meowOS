#!/usr/bin/env bash
set -euo pipefail

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
        with open(f'/tmp/build-phase-{k}.sh', 'w') as f:
            f.write(script)
        os.chmod(f'/tmp/build-phase-{k}.sh', 0o755)
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
src_cache="/tmp/meow-source-cache"
mkdir -p "$src_cache"
src_tar="$src_cache/$(basename "$src_url")"

if [ ! -f "$src_tar" ]; then
    echo "downloading $src_url ..."
    curl -sL "$src_url" -o "$src_tar"
fi

# --- set up directories ---
build_root="/tmp/meow-build-$$"
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
    script_file="/tmp/build-phase-$phase.sh"
    if [ -f "$script_file" ]; then
        echo "--- phase: $phase ---"
        bash -e "$script_file" 2>&1 || true
        rm -f "$script_file"
    fi
done

# --- package with meow-build ---
if [ -d "$out/usr" ]; then
    mkdir -p "$pkg_dir/files"
    cp -a "$out/usr" "$pkg_dir/files/"
fi

if [ -z "$output_dir" ]; then
    output_dir="/tmp/meow-pkg-out"
fi
mkdir -p "$output_dir"

./build/meow-build --output "$output_dir" "$pkg_dir"

rm -rf "$build_root"
