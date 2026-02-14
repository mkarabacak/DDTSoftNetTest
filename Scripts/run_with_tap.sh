#!/bin/bash
# DDTSoftNetTest - QEMU with TAP networking (for Companion tests)
# Onkosul: sudo ./setup_tap.sh calistirilmis olmali
set -e

WORKSPACE_ROOT="/home/mahir-karabacak/workspace"
EDK2_DIR="$WORKSPACE_ROOT/edk2"
DDTSOFT_DIR="$WORKSPACE_ROOT/DDTSoft"
EFI_IMG="$WORKSPACE_ROOT/efi_disk.img"
EFI_MNT="$WORKSPACE_ROOT/efi_mnt"
OVMF_CODE="/usr/share/OVMF/OVMF_CODE_4M.fd"
OVMF_VARS_SRC="/usr/share/OVMF/OVMF_VARS_4M.fd"
OVMF_VARS="$WORKSPACE_ROOT/ovmf_vars.fd"

cd "$EDK2_DIR"
source edksetup.sh
export PACKAGES_PATH="$EDK2_DIR:$DDTSOFT_DIR"

echo "=== Building DDTSoftNetTest ==="
build -a X64 -t GCC5 -p DDTSoftNetTest/DDTSoftNetTest.dsc -b DEBUG

echo "=== Creating EFI disk image ==="
dd if=/dev/zero of="$EFI_IMG" bs=1M count=64 2>/dev/null
mkfs.vfat -F 32 "$EFI_IMG" >/dev/null
mkdir -p "$EFI_MNT"
sudo mount "$EFI_IMG" "$EFI_MNT"
sudo mkdir -p "$EFI_MNT/EFI/BOOT"
sudo cp Build/DDTSoftNetTest/DEBUG_GCC5/X64/DDTSoftNetTest.efi "$EFI_MNT/EFI/BOOT/BOOTX64.EFI"
sudo cp Build/DDTSoftNetTest/DEBUG_GCC5/X64/DDTSoftNetTest.efi "$EFI_MNT/DDTSoftNetTest.efi"
sudo umount "$EFI_MNT"

echo "=== Preparing OVMF ==="
cp "$OVMF_VARS_SRC" "$OVMF_VARS"

echo "=== Launching QEMU (TAP net: 192.168.100.0/24) ==="
echo "    Uygulama otomatik baslatilir (BOOTX64.EFI)"
echo "    DUT IP:        192.168.100.10"
echo "    Companion IP:  192.168.100.1"
echo "    Cikis:         Ctrl+A, X"
echo ""

qemu-system-x86_64 \
  -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
  -drive if=pflash,format=raw,file="$OVMF_VARS" \
  -drive format=raw,file="$EFI_IMG" \
  -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
  -device e1000,netdev=net0 \
  -m 512M \
  -nographic
