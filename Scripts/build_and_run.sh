#!/bin/bash
# DDTSoftNetTest - Build and run in QEMU (user-mode networking)
# Kullanim: ./build_and_run.sh
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

RELEASE_DIR="$DDTSOFT_DIR/DDTSoftNetTest/Release"
mkdir -p "$RELEASE_DIR"
cp Build/DDTSoftNetTest/DEBUG_GCC5/X64/DDTSoftNetTest.efi "$RELEASE_DIR/"
echo "=== Release updated ==="

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

echo "=== Launching QEMU (user-mode net) ==="
echo "    Uygulama otomatik baslatilir (BOOTX64.EFI)"
echo "    Cikis: Ctrl+A, X"
echo ""

qemu-system-x86_64 \
  -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
  -drive if=pflash,format=raw,file="$OVMF_VARS" \
  -drive format=raw,file="$EFI_IMG" \
  -net nic,model=e1000 \
  -net user \
  -m 512M \
  -nographic
