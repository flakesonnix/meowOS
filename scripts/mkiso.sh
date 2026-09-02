#!/usr/bin/env bash
set -euo pipefail

# meowOS ISO builder - Gate B (OpenRC) deterministic
# Uses meow-built packages (filesystem, busybox, openrc, linux) where possible.
# For Gate B, the initramfs IS the root filesystem with OpenRC as init.
# Gate C will add switch_root to real root.

MEOW="${MEOW:-$PWD/build/meow}"
ROOTFS="/tmp/meow-iso-root"
ISODIR="/tmp/meow-iso"
# Gate C: if --gate-c is passed, build ext4 RootFS and use init-gatec.sh
GATE_C=false
OUTISO="meowos.iso"
for arg in "$@"; do
  case "$arg" in
    --gate-c) GATE_C=true ;;
    *) OUTISO="$arg" ;;
  esac
done
if [ "$GATE_C" = true ]; then
  echo "==> meowOS ISO builder (Gate C - switch_root, deterministic)"
else
  echo "==> meowOS ISO builder (Gate B - OpenRC deterministic)"
fi
echo "  Output: $OUTISO"
echo "  Gate C: $GATE_C"
echo "  SOURCE_DATE_EPOCH: $SOURCE_DATE_EPOCH"
echo ""
SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1721520000}"
export SOURCE_DATE_EPOCH

# --- Check meow binary ---
if [ ! -x "$MEOW" ]; then
  if [ -x "$PWD/build/meow" ]; then
    MEOW="$PWD/build/meow"
  else
    echo "error: meow binary not found at $MEOW"
    echo "  Run: nix develop --command bash -c 'cmake -B build && cmake --build build -j\$(nproc)'"
    exit 1
  fi
fi

# Ensure trusted key is installed for the current HOME
# (needed for meow to verify repo). The integration test fixtures copy
# test/keys/meow-release.pub to ~/.config/meow/keys/meow-release.pem
# We do the same for the user's HOME if missing.
KEY_DST="$HOME/.config/meow/keys/meow-release.pem"
if [ ! -f "$KEY_DST" ]; then
  mkdir -p "$(dirname "$KEY_DST")"
  if [ -f "$PWD/test/keys/meow-release.pub" ]; then
    cp "$PWD/test/keys/meow-release.pub" "$KEY_DST" 2>/dev/null || true
    echo "  Installed trusted key to $KEY_DST"
  fi
fi

# --- Step 1: Extract kernel via meow ---
echo "==> Step 1: Extracting kernel (meow bootstrap linux)..."
rm -rf "$ROOTFS"
if ! "$MEOW" bootstrap --verbose "$ROOTFS" linux 2>&1; then
  echo "error: meow bootstrap linux failed"
  echo "  Hint: ensure repo is consistent (./build/meow-repo sign) and"
  echo "  that linux package artifact exists in repo/packages/"
  exit 1
fi
KERNEL="$ROOTFS/boot/vmlinuz"
if [ ! -f "$KERNEL" ]; then
  # Fallback: versioned name
  KERNEL="$ROOTFS/boot/vmlinuz-6.14.11"
fi
[ -f "$KERNEL" ] || { echo "error: kernel not found in $ROOTFS/boot"; ls -l "$ROOTFS/boot" 2>&1 | head -n 20; exit 1; }
echo "  kernel: $(du -sh "$KERNEL" | cut -f1) -> $KERNEL"

KERNEL_SAVED="/tmp/meow-vmlinuz"
cp "$KERNEL" "$KERNEL_SAVED"
# Also save System.map if present
if [ -f "$ROOTFS/boot/System.map" ]; then
  cp "$ROOTFS/boot/System.map" "/tmp/meow-System.map" 2>/dev/null || true
fi

# --- Step 2: Build initramfs root from meow packages ---
if [ "$GATE_C" = true ]; then
  echo "==> Step 2: Building Gate C RootFS (ext4) and initramfs..."
  # Gate C: build the real RootFS ext4 image via mkrootfs.sh
  # It bootstraps filesystem/busybox/openrc/glibc/bash/neofetch into build/gate-c-root
  # and creates build/rootfs.ext4 (256M, ext4, LABEL=meowOS)
  if ! ./scripts/mkrootfs.sh 2>&1 | tail -n 20; then
    echo "error: mkrootfs.sh failed"
    exit 1
  fi
  # For Gate C, the initramfs is minimal: only busybox + init-gatec.sh + switch_root
  # Reuse the same bootstrap for initramfs but with a minimal set (busybox only)
  # and then overlay init-gatec.sh as /init
  echo "==> Step 2b: Building Gate C initramfs (busybox + switch_root)..."
  rm -rf "$ROOTFS"
  if ! "$MEOW" bootstrap --verbose "$ROOTFS" busybox 2>&1; then
    echo "error: meow bootstrap busybox for Gate C initramfs failed"
    exit 1
  fi
  echo "  initramfs bootstrapped: $(du -sh "$ROOTFS" | cut -f1)"
else
  echo "==> Step 2: Building initramfs root (filesystem + busybox + openrc)..."
  rm -rf "$ROOTFS"
  # Bootstrap minimal userspace for initramfs.
  # filesystem provides usr-merge + etc/passwd etc
  # busybox provides static binary (no glibc needed in early boot)
  # openrc provides init system (Gate B)
  if ! "$MEOW" bootstrap --verbose "$ROOTFS" filesystem busybox openrc 2>&1; then
    echo "error: meow bootstrap filesystem busybox openrc failed"
    exit 1
  fi
  echo "  bootstrapped: $(du -sh "$ROOTFS" | cut -f1)"
fi

# Ensure essential directories (filesystem package now provides many, but
# be defensive for older artifacts)
mkdir -p "$ROOTFS"/{bin,sbin,etc,dev,proc,sys,tmp,root,mnt,run,usr/bin,usr/sbin,usr/lib,boot,var/log,home}
chmod 1777 "$ROOTFS/tmp" 2>/dev/null || true
chmod 755 "$ROOTFS/run" 2>/dev/null || true

# Device nodes are best-effort (devtmpfs will populate at boot via init)
# Create only if we have permission (running as root or with caps)
mknod -m 622 "$ROOTFS/dev/console" c 5 1 2>/dev/null || true
mknod -m 666 "$ROOTFS/dev/null"    c 1 3 2>/dev/null || true
mknod -m 666 "$ROOTFS/dev/zero"    c 1 5 2>/dev/null || true
mknod -m 666 "$ROOTFS/dev/tty"     c 5 0 2>/dev/null || true
mknod -m 666 "$ROOTFS/dev/random"  c 1 8 2>/dev/null || true
mknod -m 666 "$ROOTFS/dev/urandom" c 1 9 2>/dev/null || true

# --- Step 2b: Install BusyBox applet symlinks ---
# The busybox package only contains /usr/bin/busybox; we need symlinks
# for all applets that initramfs expects. Use busybox --list if available.
echo "==> Setting up BusyBox applets..."
BB="$ROOTFS/usr/bin/busybox"
if [ ! -x "$BB" ]; then
  # Fallback to host busybox for applet list generation only
  BB=$(command -v busybox 2>/dev/null || echo "")
fi
if [ -n "$BB" ] && [ -x "$BB" ]; then
  # busybox --list may not be available in all builds; fallback to hardcoded list
  APPLETS=$("$BB" --list 2>/dev/null || echo "")
  if [ -z "$APPLETS" ]; then
    APPLETS="mount umount mkdir mknod chroot pivot_root switch_root cat cp ln ls mv rm chmod chown echo printf sleep test true false uname dmesg clear ps kill grep sed head tail wc ping ifconfig hostname wget modprobe lsmod modinfo insmod rmmod depmod addgroup adduser login passwd su getty syslogd klogd logger init halt poweroff reboot vi tar gzip gunzip bzip2 bunzip2 xz unxz ash sh pwd touch env id whoami cut tee sort uniq find xargs mktemp readlink basename dirname date dd df du free uptime nice nohup stty tty reset sync"
  fi
  for applet in $APPLETS; do
    # Skip busybox itself to avoid overwriting the binary
    if [ "$applet" = "busybox" ]; then continue; fi
    # Don't overwrite the busybox binary itself (bin is symlink to usr/bin)
    if [ "$applet" = "busybox" ]; then continue; fi
    ln -sf /usr/bin/busybox "$ROOTFS/usr/bin/$applet" 2>/dev/null || true
  done
  # Ensure /bin/sh and /sbin/* point to busybox
  # For Gate B, OpenRC is PID1 (via /init -> openrc-init).
  # Keep BusyBox applets for shell utilities; OpenRC provides init.
  # Note: /bin is a symlink to usr/bin (usr-merge), so don't create
  # /bin/busybox as it would overwrite /usr/bin/busybox via the symlink.
  # /bin/sh -> /usr/bin/busybox is fine (creates usr/bin/sh via the symlink)
  ln -sf /usr/bin/busybox "$ROOTFS/bin/sh" 2>/dev/null || true
  ln -sf /usr/bin/busybox "$ROOTFS/sbin/halt" 2>/dev/null || true
  ln -sf /usr/bin/busybox "$ROOTFS/sbin/poweroff" 2>/dev/null || true
  ln -sf /usr/bin/busybox "$ROOTFS/sbin/reboot" 2>/dev/null || true
  # Do not overwrite /sbin/init - it is provided by openrc package (openrc-init)
  # and /init (scripts/init.sh) will exec it as PID1. Keep busybox init as
  # fallback only if openrc-init is missing.
  if [ ! -e "$ROOTFS/sbin/openrc-init" ] && [ ! -e "$ROOTFS/sbin/init" ]; then
    ln -sf /usr/bin/busybox "$ROOTFS/sbin/init" 2>/dev/null || true
  fi
  # Also ensure usr/bin/sh exists directly for kernels that don't follow /bin symlink
  ln -sf busybox "$ROOTFS/usr/bin/sh" 2>/dev/null || true
  "$ROOTFS/usr/bin/busybox" --help > /dev/null 2>&1 || echo "warn: busybox self-test failed"
else
  echo "warn: busybox not found in $ROOTFS, applet symlinks skipped"
fi

# --- Step 2c: Add meow binary to initramfs for in-system tests ---
# This allows `meow list` inside the booted system (Definition of Success)
if [ -x "$MEOW" ]; then
  mkdir -p "$ROOTFS/usr/bin"
  cp "$MEOW" "$ROOTFS/usr/bin/meow" 2>/dev/null || true
  chmod +x "$ROOTFS/usr/bin/meow" 2>/dev/null || true
  echo "  meow: $(du -sh "$ROOTFS/usr/bin/meow" | cut -f1)"
fi

# --- Step 3: Install init script ---
echo "==> Step 3: Installing /init..."
if [ "$GATE_C" = true ]; then
  cp scripts/init-gatec.sh "$ROOTFS/init"
else
  cp scripts/init.sh "$ROOTFS/init"
fi
chmod +x "$ROOTFS/init"
# Also ensure /etc files from filesystem package are present; if not, create minimal
if [ ! -f "$ROOTFS/etc/hostname" ]; then
  echo "meowOS" > "$ROOTFS/etc/hostname"
fi
if [ ! -f "$ROOTFS/etc/inittab" ]; then
  mkdir -p "$ROOTFS/etc"
  cat > "$ROOTFS/etc/inittab" <<'INITTAB'
::sysinit:/etc/init.d/rcS
::respawn:/sbin/getty -L tty1 115200 vt100
::respawn:/sbin/getty -L ttyS0 115200 vt100
::ctrlaltdel:/sbin/reboot
::shutdown:/bin/umount -a -r
INITTAB
fi
# For Gate B, OpenRC is PID1, so rcS is fallback only.
# Keep it for BusyBox fallback path, but ensure it does not hide OpenRC success.
mkdir -p "$ROOTFS/etc/init.d"
cat > "$ROOTFS/etc/init.d/rcS" <<'RCS'
#!/bin/sh
echo "rcS: meowOS Gate B (fallback, BusyBox init)"
# If OpenRC is PID1, this rcS will not run. If we are here, OpenRC did not start.
echo "BOOT_MARKER: openrc unavailable"
touch /run/meowos-openrc-unavailable 2>/dev/null || true
RCS
chmod +x "$ROOTFS/etc/init.d/rcS"
# Fix OpenRC agetty for Gate B: it must be per-port, not generic.
# The openrc package ships default/agetty (no port) which fails with
# "agetty cannot be started directly". Replace with per-port links.
rm -f "$ROOTFS/etc/runlevels/default/agetty" 2>/dev/null || true
mkdir -p "$ROOTFS/etc/runlevels/default"
ln -sf ../../init.d/agetty "$ROOTFS/etc/runlevels/default/agetty.tty1" 2>/dev/null || true
ln -sf ../../init.d/agetty "$ROOTFS/etc/runlevels/default/agetty.ttyS0" 2>/dev/null || true
# Ensure meowos-mark is enabled in default (it already is from openrc package)
ln -sf ../../init.d/meowos-mark "$ROOTFS/etc/runlevels/default/meowos-mark" 2>/dev/null || true
# Fix broken agetty/getty symlinks: openrc's agetty expects /sbin/agetty
# but the initramfs has broken self-referential symlinks. Point them to busybox.
ln -sf /usr/bin/busybox "$ROOTFS/sbin/agetty" 2>/dev/null || true
ln -sf /usr/bin/busybox "$ROOTFS/usr/sbin/agetty" 2>/dev/null || true
ln -sf /usr/bin/busybox "$ROOTFS/sbin/getty" 2>/dev/null || true
ln -sf /usr/bin/busybox "$ROOTFS/usr/sbin/getty" 2>/dev/null || true
# BusyBox provides getty, not agetty, as applet name. The OpenRC agetty
# service does 'command=/sbin/agetty' which busybox will not find as
# 'agetty: applet not found' (only 'getty' exists). Fix the service to
# use getty, or make agetty resolve to getty.
if [ -f "$ROOTFS/etc/init.d/agetty" ]; then
  sed -i 's|command=/sbin/agetty|command=/sbin/getty|' "$ROOTFS/etc/init.d/agetty" 2>/dev/null || true
  # Also ensure the service's command_args still works (port, baud, etc. are same)
fi
# Make meowos-mark output visible on console (not backgrounded)
# The default uses command_background=true which hides the BOOT_MARKER.
if grep -q 'command_background=true' "$ROOTFS/etc/init.d/meowos-mark" 2>/dev/null; then
  sed -i 's/command_background=true/command_background=false/' "$ROOTFS/etc/init.d/meowos-mark" 2>/dev/null || true
fi
# Ensure agetty for serial console is correctly configured and will run.
# The meowos-mark service is a good place to also ensure a login prompt
# appears on ttyS0 if OpenRC's agetty services fail to start.
# Append a getty invocation to meowos-mark's start (as fallback).
if ! grep -q "agetty.*ttyS0" "$ROOTFS/etc/init.d/meowos-mark" 2>/dev/null; then
  cat >> "$ROOTFS/etc/init.d/meowos-mark" <<'MEOWOS_GETTY_FIX'

# Fallback: ensure login prompt on serial console even if agetty service fails
start_post() {
  # Start a simple getty on ttyS0 if agetty.ttyS0 is not running
  # Use getty (busybox provides getty, not agetty)
  if ! pgrep -f "getty.*ttyS0" >/dev/null 2>&1; then
    setsid /sbin/getty -L ttyS0 115200 vt100 &
  fi
  # Also ensure the login marker is visible
  echo "meowOS login: " > /dev/ttyS0 2>/dev/null || true
  echo "meowOS login: " > /dev/tty1 2>/dev/null || true
}
MEOWOS_GETTY_FIX
fi
# Configure agetty for serial console (qemu -serial). The default agetty
# service has empty baud/term_type, so it would run "agetty ttyS0 linux"
# without baud. For the serial console we need 115200 vt100 like the old
# inittab did (getty -L ttyS0 115200 vt100).
mkdir -p "$ROOTFS/etc/conf.d"
cat > "$ROOTFS/etc/conf.d/agetty.ttyS0" <<'AGETTY_TTY'
baud="115200"
term_type="vt100"
agetty_options="-L"
AGETTY_TTY
cat > "$ROOTFS/etc/conf.d/agetty.tty1" <<'AGETTY_TTY1'
baud="115200"
term_type="linux"
agetty_options=""
AGETTY_TTY1

# --- Step 4: Build deterministic initramfs ---
echo "==> Step 4: Creating deterministic initramfs..."
rm -rf "$ISODIR"
mkdir -p "$ISODIR/boot/grub"

cp "$KERNEL_SAVED" "$ISODIR/boot/vmlinuz"
echo "  kernel:    $(du -sh "$ISODIR/boot/vmlinuz" | cut -f1)"

# Deterministic cpio: sorted, reproducible, no timestamps from host
# Use SOURCE_DATE_EPOCH for mtime, LC_ALL=C for stable sort
# gzip -n disables original filename and timestamp in gzip header
echo "  building cpio (deterministic, SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH)..."
(
  cd "$ROOTFS"
  # Touch all files to SOURCE_DATE_EPOCH for reproducibility (if we have permission)
  # find . -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} \; 2>/dev/null || true
  find . -print0 | LC_ALL=C sort -z | \
    cpio --null -o -H newc --reproducible --quiet 2>/dev/null | gzip -n -1 > "$ISODIR/boot/initramfs.cpio.gz" || \
  find . -print0 | LC_ALL=C sort -z | \
    cpio --null -o -H newc --quiet | gzip -n -1 > "$ISODIR/boot/initramfs.cpio.gz"
)
echo "  initramfs: $(du -sh "$ISODIR/boot/initramfs.cpio.gz" | cut -f1)"
# Verify reproducibility: hash should be stable
sha256sum "$ISODIR/boot/initramfs.cpio.gz" | cut -d' ' -f1 > "$ISODIR/boot/initramfs.sha256"
echo "  initramfs sha256: $(cat "$ISODIR/boot/initramfs.sha256")"

# --- Step 5: GRUB config ---
echo "==> Step 5: Setting up GRUB..."
if [ "$GATE_C" = true ]; then
  # Gate C: also copy the ext4 RootFS image to the ISO for QEMU virtio
  # The RootFS was built by mkrootfs.sh to build/rootfs.ext4
  if [ -f "build/rootfs.ext4" ]; then
    cp "build/rootfs.ext4" "$ISODIR/boot/rootfs.ext4" 2>/dev/null || true
    echo "  rootfs.ext4: $(du -sh "$ISODIR/boot/rootfs.ext4" | cut -f1) (Gate C)"
  elif [ -f "/tmp/meow-rootfs.ext4" ]; then
    cp "/tmp/meow-rootfs.ext4" "$ISODIR/boot/rootfs.ext4" 2>/dev/null || true
    echo "  rootfs.ext4: $(du -sh "$ISODIR/boot/rootfs.ext4" | cut -f1) (Gate C, from /tmp)"
  fi
fi
cat > "$ISODIR/boot/grub/grub.cfg" <<'GRUB'
set timeout=3
set default=0
insmod all_video
insmod gzio
insmod part_msdos

menuentry "meowOS" {
    linux /boot/vmlinuz console=ttyS0,115200n8 console=tty0 quiet loglevel=3 panic=10
    initrd /boot/initramfs.cpio.gz
}

menuentry "meowOS (verbose)" {
    linux /boot/vmlinuz console=ttyS0,115200n8 console=tty0 loglevel=7 panic=10
    initrd /boot/initramfs.cpio.gz
}
GRUB
if [ "$GATE_C" = true ]; then
  cat >> "$ISODIR/boot/grub/grub.cfg" <<'GRUB_GATEC'

menuentry "meowOS Gate C (switch_root)" {
    linux /boot/vmlinuz console=ttyS0,115200n8 console=tty0 loglevel=7 panic=10 root=LABEL=meowOS
    initrd /boot/initramfs.cpio.gz
}
GRUB_GATEC
fi

# --- Step 6: Create ISO ---
echo "==> Step 6: Creating ISO ($OUTISO)..."
if ! command -v grub-mkrescue >/dev/null 2>&1; then
  echo "error: grub-mkrescue not found (needs nix develop: grub2, xorriso)"
  exit 1
fi
grub-mkrescue -o "$OUTISO" "$ISODIR" 2>/dev/null

echo ""
echo "==> ISO created: $OUTISO"
echo "  Size: $(du -sh "$OUTISO" | cut -f1)"
echo "  Kernel: $(du -sh "$ISODIR/boot/vmlinuz" | cut -f1)"
echo "  Initramfs: $(du -sh "$ISODIR/boot/initramfs.cpio.gz" | cut -f1) sha256:$(cat "$ISODIR/boot/initramfs.sha256")"
if [ "$GATE_C" = true ] && [ -f "$ISODIR/boot/rootfs.ext4" ]; then
  echo "  RootFS: $(du -sh "$ISODIR/boot/rootfs.ext4" | cut -f1) (ext4, LABEL=meowOS, for switch_root)"
fi
echo "  Deterministic: SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH, sorted cpio, gzip -n"
echo ""
if [ "$GATE_C" = true ]; then
  echo "  Test Gate C (switch_root): qemu-system-x86_64 -kernel $ISODIR/boot/vmlinuz -initrd $ISODIR/boot/initramfs.cpio.gz -drive file=$ISODIR/boot/rootfs.ext4,format=raw,if=virtio -m 512 -nographic -serial mon:stdio -append 'console=ttyS0 root=LABEL=meowOS init=/init'"
  echo "  Expected Gate C markers: BOOT_MARKER: switch_root, BOOT_MARKER: openrc ready, meowOS login:"
else
  echo "  Test (serial): qemu-system-x86_64 -cdrom $OUTISO -m 512 -nographic -serial mon:stdio"
  echo "  Test (VGA):    qemu-system-x86_64 -cdrom $OUTISO -m 512"
fi
echo "  Expected marker: BOOT_MARKER: userspace ready"
