#!/bin/sh
# meowOS Gate A init - deterministic, BusyBox initramfs
# Mounts virtual filesystems, sets hostname, prints boot marker,
# then hands off to BusyBox init (which reads /etc/inittab).
# For initramfs-only Gate A, this IS the root filesystem.

# Use BusyBox applets via /usr/bin/busybox where needed; symlinks
# in /bin and /usr/bin cover the rest once /proc/sys are up.
BUSYBOX="/usr/bin/busybox"
if [ ! -x "$BUSYBOX" ]; then
  BUSYBOX="/bin/busybox"
fi

$BUSYBOX mount -t proc     none  /proc 2>/dev/null || mount -t proc none /proc
$BUSYBOX mount -t sysfs    none  /sys 2>/dev/null || mount -t sysfs none /sys
$BUSYBOX mount -t devtmpfs none  /dev 2>/dev/null || mount -t devtmpfs none /dev
$BUSYBOX mkdir -p /dev/pts
$BUSYBOX mount -t devpts  none  /dev/pts 2>/dev/null || mount -t devpts none /dev/pts
$BUSYBOX mount -t tmpfs    none  /tmp 2>/dev/null || mount -t tmpfs none /tmp
$BUSYBOX mkdir -p /run
$BUSYBOX mount -t tmpfs    none  /run 2>/dev/null || mount -t tmpfs none /run

# Ensure /etc/hostname exists and set hostname
if [ -f /etc/hostname ]; then
  $BUSYBOX hostname -F /etc/hostname 2>/dev/null || $BUSYBOX hostname meowOS
else
  $BUSYBOX hostname meowOS
fi

# Deterministic banner for QEMU tests
echo ""
echo "  __  __                      ____   _____ "
echo " |  \/  |                    / __ \ / ____|"
echo " | \  / | ___  ___  ___  ___| |  | | (___  "
echo " | |\/| |/ _ \/ __|/ _ \/ __| |  | |\___ \ "
echo " | |  | | (_) \__ \  __/\__ \ |__| |____) |"
echo " |_|  |_|\___/|___/\___||___/\____/|_____/ "
echo ""
echo "  Welcome to meowOS!"
echo ""
echo "BOOT_MARKER: userspace ready"
echo "init: handing off to /sbin/init"

# If we are initramfs-only, /sbin/init is BusyBox init which will parse
# /etc/inittab and spawn getty on ttyS0 / tty1.
# Fall back to shell if init not present (debug).
if [ -x /sbin/init ]; then
  exec /sbin/init
else
  echo "WARN: /sbin/init not found, falling back to /bin/sh"
  exec /bin/sh
fi
