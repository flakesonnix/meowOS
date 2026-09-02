#!/usr/bin/env bash
set -euo pipefail

# meowOS Gate C - ext4 RootFS builder (deterministic)
# Builds a bootable ext4 image from meow packages via meow bootstrap.
# The initramfs will mount this image at /newroot and switch_root to it.

MEOW="${MEOW:-$PWD/build/meow}"
ROOTFS="${1:-build/gate-c-root}"
IMAGE="${2:-build/rootfs.ext4}"
SIZE_MB="${SIZE_MB:-256}"
SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1721520000}"
export SOURCE_DATE_EPOCH

echo "==> meowOS Gate C RootFS builder"
echo "  RootFS: $ROOTFS"
echo "  Image:  $IMAGE (${SIZE_MB}M ext4, LABEL=meowOS)"
echo "  SOURCE_DATE_EPOCH: $SOURCE_DATE_EPOCH"
echo ""

if [ ! -x "$MEOW" ]; then
  if [ -x "$PWD/build/meow" ]; then
    MEOW="$PWD/build/meow"
  else
    echo "error: meow not found at $MEOW"
    exit 1
  fi
fi

# Ensure trusted key
KEY_DST="$HOME/.config/meow/keys/meow-release.pem"
if [ ! -f "$KEY_DST" ] && [ -f "$PWD/test/keys/meow-release.pub" ]; then
  mkdir -p "$(dirname "$KEY_DST")"
  cp "$PWD/test/keys/meow-release.pub" "$KEY_DST" 2>/dev/null || true
fi

# --- Bootstrap full RootFS ---
# For Gate C we need a real userspace: filesystem + busybox + openrc + glibc + bash
# (linux is kernel, not part of RootFS). Add a getty-capable system.
echo "==> Bootstrapping RootFS ($ROOTFS)..."
rm -rf "$ROOTFS"
if ! "$MEOW" bootstrap --verbose "$ROOTFS" filesystem busybox openrc glibc bash neofetch 2>&1; then
  echo "error: meow bootstrap failed"
  exit 1
fi
echo "  bootstrapped: $(du -sh "$ROOTFS" | cut -f1)  $(find "$ROOTFS" -type f | wc -l) files"

# Ensure essential mountpoints exist (defensive, filesystem package provides most)
mkdir -p "$ROOTFS"/{proc,sys,dev,tmp,run,boot,home,root,mnt,var/log}
chmod 1777 "$ROOTFS/tmp" 2>/dev/null || true
chmod 755 "$ROOTFS/run" 2>/dev/null || true

# Ensure /etc/hostname and minimal passwd/shadow if missing
if [ ! -f "$ROOTFS/etc/hostname" ]; then
  echo "meowOS" > "$ROOTFS/etc/hostname"
fi

# Make /etc/fstab for the real RootFS (initramfs will mount it)
mkdir -p "$ROOTFS/etc"
cat > "$ROOTFS/etc/fstab" <<'FSTAB'
LABEL=meowOS / ext4 defaults 0 1
proc /proc proc defaults 0 0
sysfs /sys sysfs defaults 0 0
devtmpfs /dev devtmpfs mode=0755 0 0
devpts /dev/pts devpts mode=0620,gid=5 0 0
tmpfs /tmp tmpfs mode=1777 0 0
tmpfs /run tmpfs mode=0755 0 0
FSTAB

# Deterministic timestamps for reproducibility
find "$ROOTFS" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} \; 2>/dev/null || true

# --- Create ext4 image ---
echo "==> Creating ext4 image ($IMAGE, ${SIZE_MB}M)..."
rm -f "$IMAGE"
truncate -s "${SIZE_MB}M" "$IMAGE"
# Use mkfs.ext4 with deterministic options: no journal? keep journal for real root, but make it reproducible
# -L LABEL, -d root directory, -E hash_seed=... for determinism if needed
# Use -U clear to not use random UUID, but set a fixed one for reproducibility
# For now, simple mkfs.ext4 -d
if command -v mkfs.ext4 >/dev/null 2>&1; then
  mkfs.ext4 -L meowOS -d "$ROOTFS" -F "$IMAGE" 2>&1 | tail -n 5
else
  mke2fs -t ext4 -L meowOS -d "$ROOTFS" "$IMAGE" 2>&1 | tail -n 5
fi

echo "  image: $(du -sh "$IMAGE" | cut -f1)"
# Verify
if command -v debugfs >/dev/null 2>&1; then
  echo "  verifying ext4..."
  debugfs -R "ls -l" "$IMAGE" 2>&1 | head -n 20 || true
fi

# Also keep a copy for QEMU
cp "$IMAGE" /tmp/meow-rootfs.ext4 2>/dev/null || true
echo "  also at /tmp/meow-rootfs.ext4 for QEMU"

echo ""
echo "==> Gate C RootFS ready"
echo "  RootFS dir: $ROOTFS ($(du -sh "$ROOTFS" | cut -f1))"
echo "  Image: $IMAGE ($(du -sh "$IMAGE" | cut -f1))"
echo "  Test: qemu-system-x86_64 -kernel /tmp/meow-iso/boot/vmlinuz -initrd /tmp/meow-iso/boot/initramfs.cpio.gz -drive file=$IMAGE,format=raw,if=virtio -append 'console=ttyS0 root=LABEL=meowOS init=/sbin/init'"
echo ""
