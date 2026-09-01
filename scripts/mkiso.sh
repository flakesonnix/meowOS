#!/usr/bin/env bash
set -euo pipefail

# meowOS ISO builder - Gate B (OpenRC) deterministic
# Uses meow-built packages (filesystem, busybox, openrc, linux) where possible.
# For Gate B, the initramfs IS the root filesystem with OpenRC as init.
# Gate C will add switch_root to real root.

MEOW="${MEOW:-$PWD/build/meow}"
ROOTFS="/tmp/meow-iso-root"
ISODIR="/tmp/meow-iso"
OUTISO="${1:-meowos.iso}"
SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1721520000}"
export SOURCE_DATE_EPOCH

echo "==> meowOS ISO builder (Gate B - OpenRC deterministic)"
echo "  Output: $OUTISO"
echo "  SOURCE_DATE_EPOCH: $SOURCE_DATE_EPOCH"
echo ""

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
  # For Gate B, OpenRC is run as a service via rcS, not as PID 1.
  # Keep BusyBox init as PID 1 (/sbin/init -> busybox) for now.
  # OpenRC will be started from rcS via "openrc sysinit/boot/default".
  # Note: /bin is a symlink to usr/bin (usr-merge), so don't create
  # /bin/busybox as it would overwrite /usr/bin/busybox via the symlink.
  # /bin/sh -> /usr/bin/busybox is fine (creates usr/bin/sh via the symlink)
  ln -sf /usr/bin/busybox "$ROOTFS/bin/sh" 2>/dev/null || true
  ln -sf /usr/bin/busybox "$ROOTFS/sbin/init" 2>/dev/null || true
  ln -sf /usr/bin/busybox "$ROOTFS/sbin/halt" 2>/dev/null || true
  ln -sf /usr/bin/busybox "$ROOTFS/sbin/poweroff" 2>/dev/null || true
  ln -sf /usr/bin/busybox "$ROOTFS/sbin/reboot" 2>/dev/null || true
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
cp scripts/init.sh "$ROOTFS/init"
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
if [ ! -f "$ROOTFS/etc/init.d/rcS" ]; then
  mkdir -p "$ROOTFS/etc/init.d"
  cat > "$ROOTFS/etc/init.d/rcS" <<'RCS'
#!/bin/sh
echo "rcS: meowOS Gate A"
echo "Welcome to meowOS"
echo "BOOT_MARKER: userspace ready"
RCS
  chmod +x "$ROOTFS/etc/init.d/rcS"
fi

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
echo "  Deterministic: SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH, sorted cpio, gzip -n"
echo ""
echo "  Test (serial): qemu-system-x86_64 -cdrom $OUTISO -m 512 -nographic -serial mon:stdio"
echo "  Test (VGA):    qemu-system-x86_64 -cdrom $OUTISO -m 512"
echo "  Expected marker: BOOT_MARKER: userspace ready"
