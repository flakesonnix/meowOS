#!/usr/bin/env bash
set -euo pipefail

MEOW="$PWD/build/meow"
ROOTFS="/tmp/meow-iso-root"
ISODIR="/tmp/meow-iso"
OUTISO="${1:-meowos.iso}"

echo "==> meowOS ISO builder"
echo "  Output: $OUTISO"
echo ""

# Step 1: Get kernel from meow repo (bootstrap linux only)
echo "==> Extracting kernel..."
rm -rf "$ROOTFS"
"$MEOW" bootstrap --verbose "$ROOTFS" linux 2>&1
KERNEL="$ROOTFS/boot/vmlinuz"
[ -f "$KERNEL" ] || { echo "error: kernel not found"; exit 1; }
echo "  kernel: $(du -sh "$KERNEL" | cut -f1)"

# Save kernel path before rebuilding rootfs
KERNEL_SAVED=/tmp/meow-vmlinuz
cp "$KERNEL" "$KERNEL_SAVED"

rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"/{bin,sbin,etc,dev,proc,sys,tmp,root,mnt,run,usr/bin,usr/sbin,usr/lib,boot}
# Create static device nodes for early boot
mknod -m 622 "$ROOTFS/dev/console" c 5 1 2>/dev/null || true
mknod -m 666 "$ROOTFS/dev/null"    c 1 3 2>/dev/null || true
mknod -m 666 "$ROOTFS/dev/zero"    c 1 5 2>/dev/null || true
mknod -m 666 "$ROOTFS/dev/tty"     c 5 0 2>/dev/null || true
mknod -m 666 "$ROOTFS/dev/random"  c 1 8 2>/dev/null || true
mknod -m 644 "$ROOTFS/dev/urandom" c 1 9 2>/dev/null || true

# Step 2: Get static busybox from nixpkgs
echo "==> Getting busybox..."
BB=$(nix shell nixpkgs#busybox -c which busybox 2>/dev/null || echo "")
[ -n "$BB" ] || { echo "error: busybox not available"; exit 1; }
echo "  busybox: $BB"

cp "$BB" "$ROOTFS/usr/bin/busybox"
chmod +x "$ROOTFS/usr/bin/busybox"

for applet in mount umount mkdir mknod chroot pivot_root switch_root \
              cat cp ln ls mv rm chmod chown echo printf sleep test true false \
              uname dmesg clear ps kill grep sed head tail wc \
              ping ifconfig hostname wget \
              modprobe lsmod modinfo insmod rmmod depmod \
              addgroup adduser login passwd su getty \
              syslogd klogd logger init halt poweroff reboot \
              vi tar gzip gunzip bzip2 bunzip2 xz unxz \
              ash sh pwd touch env id whoami \
              cut tee sort uniq find xargs mktemp readlink \
              basename dirname date dd df du free uptime \
              nice nohup stty tty reset sync ; do
    ln -sf /usr/bin/busybox "$ROOTFS/usr/bin/$applet"
done

ln -sf /usr/bin/busybox "$ROOTFS/bin/sh"
ln -sf /usr/bin/busybox "$ROOTFS/bin/busybox"
ln -sf /usr/bin/busybox "$ROOTFS/sbin/init"
ln -sf /usr/bin/busybox "$ROOTFS/sbin/halt"
ln -sf /usr/bin/busybox "$ROOTFS/sbin/poweroff"
ln -sf /usr/bin/busybox "$ROOTFS/sbin/reboot"

"$ROOTFS/usr/bin/busybox" --help > /dev/null 2>&1 || {
    echo "error: busybox doesn't work"
    exit 1
}

# Step 3: Init script
echo "==> Setting up init..."
cp scripts/init.sh "$ROOTFS/init"
chmod +x "$ROOTFS/init"

cat > "$ROOTFS/etc/hostname" << 'HOST'
meowOS
HOST

# Step 4: Build initramfs
echo "==> Creating initramfs..."
rm -rf "$ISODIR"
mkdir -p "$ISODIR/boot/grub"

cp "$KERNEL_SAVED" "$ISODIR/boot/vmlinuz"
echo "  kernel:    $(du -sh "$ISODIR/boot/vmlinuz" | cut -f1)"

(cd "$ROOTFS" && find . -print0 | \
  cpio --null -o -H newc --quiet | gzip -1 > "$ISODIR/boot/initramfs.cpio.gz")
echo "  initramfs: $(du -sh "$ISODIR/boot/initramfs.cpio.gz" | cut -f1)"

# Step 5: GRUB config
echo "==> Setting up GRUB..."
cat > "$ISODIR/boot/grub/grub.cfg" << 'GRUB'
set timeout=3
set default=0
insmod all_video
insmod gzio
insmod part_msdos

menuentry "meowOS" {
    linux /boot/vmlinuz console=ttyS0 quiet loglevel=3 panic=10
    initrd /boot/initramfs.cpio.gz
}

menuentry "meowOS (verbose)" {
    linux /boot/vmlinuz console=ttyS0 loglevel=7 panic=10
    initrd /boot/initramfs.cpio.gz
}
GRUB

# Step 6: Create ISO
echo "==> Creating ISO ($OUTISO)..."
grub-mkrescue -o "$OUTISO" "$ISODIR" 2>/dev/null

echo ""
echo "==> ISO created: $OUTISO"
echo "  Size: $(du -sh "$OUTISO" | cut -f1)"
echo "  Boot: qemu-system-x86_64 -cdrom $OUTISO -m 2G"
