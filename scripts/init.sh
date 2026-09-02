#!/bin/sh
# meowOS Gate B init - OpenRC as PID1
# Mounts virtual filesystems, sets hostname, starts OpenRC as init.
# After OpenRC initializes, agetty provides the login prompt.

BUSYBOX="/usr/bin/busybox"
if [ ! -x "$BUSYBOX" ]; then
  BUSYBOX="/bin/busybox"
fi

# Mount virtual filesystems
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

echo "rcS: meowOS Gate B"
# Deterministic banner
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
echo "init: starting OpenRC as PID1"

# Start OpenRC as PID1
# Try openrc-init first, fall back to busybox init
if [ -x /sbin/openrc-init ]; then
  exec /sbin/openrc-init 2>&1
else
  # Fall back to busybox init
  exec /sbin/init 2>&1
fi
