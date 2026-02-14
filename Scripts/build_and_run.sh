#!/bin/bash
set -e
cd ~/edk2
source edksetup.sh
export PACKAGES_PATH=$HOME/edk2:$HOME/DDTSoft
echo "=== Building DDTSoftNetTest ==="
build -a X64 -t GCC5 -p DDTSoftNetTest/DDTSoftNetTest.dsc -b DEBUG
echo "=== Copying to EFI disk ==="
mkdir -p ~/efi_disk
cp Build/DDTSoftNetTest/DEBUG_GCC5/X64/DDTSoftNetTest.efi ~/efi_disk/
echo "=== Launching QEMU ==="
qemu-system-x86_64 \
  -bios /usr/share/OVMF/OVMF_CODE.fd \
  -drive format=raw,file=fat:rw:$HOME/efi_disk \
  -net nic,model=e1000 \
  -net user \
  -m 512M \
  -nographic
