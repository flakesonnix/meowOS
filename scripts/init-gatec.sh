#!/bin/sh
# meowOS Gate C initramfs - minimal, mounts real RootFS and switch_root
# This is PID1 in the initramfs. It mounts the ext4 RootFS at /newroot
# and then switch_root to it, where OpenRC runs as PID1 in the real RootFS.

BUSYBOX="/usr/bin/busybox"
if [ ! -x "$BUSYBOX" ]; then
  BUSYBOX="/bin/busybox"
fi

# Mount virtual filesystems for the initramfs stage
$BUSYBOX mount -t proc none /proc 2>/dev/null || mount -t proc none /proc
$BUSYBOX mount -t sysfs none /sys 2>/dev/null || mount -t sysfs none /sys
$BUSYBOX mount -t devtmpfs none /dev 2>/dev/null || mount -t devtmpfs none /dev
$BUSYBOX mkdir -p /dev/pts
$BUSYBOX mount -t devpts none /dev/pts 2>/dev/null || mount -t devpts none /dev/pts

echo "rcS: meowOS Gate B"
# Wait for the root device to appear (virtio-blk, sda, vda)
echo "Gate C initramfs: waiting for root device..."
# Debug: list available block devices
echo "Gate C initramfs: available block devices:"
$BUSYBOX ls -l /dev/vda* /dev/sda* /dev/disk/by-label/* 2>&1 | head -n 20
$BUSYBOX dmesg 2>&1 | grep -i "virtio\|vda\|sda" | head -n 10
for i in 1 2 3 4 5 6 7 8 9 10; do
  for dev in /dev/vda /dev/sda /dev/disk/by-label/meowOS /dev/vda1 /dev/sda1; do
    if [ -e "$dev" ]; then
      ROOTDEV="$dev"
      break 2
    fi
  done
  echo "  waiting for root device... $i (checked vda, sda, by-label)"
  $BUSYBOX sleep 1
done

if [ -z "${ROOTDEV:-}" ]; then
  echo "Gate C initramfs: no root device found after 10s, listing /dev"
  $BUSYBOX ls -l /dev/ 2>&1 | head -n 30
  echo "Gate C initramfs: falling back to /dev/vda"
  ROOTDEV="/dev/vda"
fi

echo "Gate C initramfs: found root at $ROOTDEV"

# Create mountpoint and mount the real RootFS
$BUSYBOX mkdir -p /newroot
echo "Gate C initramfs: mounting $ROOTDEV at /newroot..."
if ! $BUSYBOX mount -t ext4 -o ro "$ROOTDEV" /newroot 2>&1; then
  echo "Gate C initramfs: mount failed, trying without -t"
  $BUSYBOX mount -o ro "$ROOTDEV" /newroot 2>&1 || {
    echo "Gate C initramfs: FATAL: cannot mount rootfs"
    echo "Gate C initramfs: dropping to emergency shell"
    exec /bin/sh
  }
fi

# Verify the new root looks valid
if [ ! -e /newroot/sbin/init ]; then
  echo "Gate C initramfs: WARNING: /newroot/sbin/init not found"
  ls -l /newroot/sbin/ 2>&1 | head -n 10
fi
if [ ! -e /newroot/usr/bin/busybox ]; then
  echo "Gate C initramfs: WARNING: /newroot/usr/bin/busybox not found"
fi

echo "Gate C initramfs: RootFS mounted at /newroot, preparing switch_root..."

# Move virtual filesystems to the new root
$BUSYBOX mkdir -p /newroot/proc /newroot/sys /newroot/dev /newroot/run 2>/dev/null || true
$BUSYBOX mount --move /proc /newroot/proc 2>&1 || echo "  move /proc failed"
$BUSYBOX mount --move /sys /newroot/sys 2>&1 || echo "  move /sys failed"
$BUSYBOX mount --move /dev /newroot/dev 2>&1 || echo "  move /dev failed"
$BUSYBOX mount --move /dev/pts /newroot/dev/pts 2>&1 || true

echo "Gate C initramfs: switching root to /newroot /sbin/init..."
echo "BOOT_MARKER: switch_root to real RootFS"

# Ensure switch_root exists (via busybox)
if [ ! -x /usr/bin/busybox ]; then
  echo "Gate C initramfs: busybox not found for switch_root"
  exec /bin/sh
fi

# Use busybox switch_root
exec /usr/bin/busybox switch_root /newroot /sbin/init

# Fallback if switch_root fails
echo "Gate C initramfs: switch_root failed, dropping to shell"
exec /bin/sh
