#!/bin/sh

/bin/busybox mount -t proc     none  /proc
/bin/busybox mount -t sysfs    none  /sys
/bin/busybox mount -t devtmpfs none  /dev
/bin/busybox mkdir -p /dev/pts
/bin/busybox mount -t devpts  none  /dev/pts
/bin/busybox mount -t tmpfs    none  /tmp
/bin/busybox mkdir -p /run
/bin/busybox mount -t tmpfs    none  /run

/bin/busybox hostname meowOS

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

exec /bin/sh
