#!/bin/bash
set -e
cd ~/edk2
source edksetup.sh
export PACKAGES_PATH=$HOME/edk2:$HOME/DDTSoft
echo "=== Building DDTSoftNetTest ==="
build -a X64 -t GCC5 -p DDTSoftNetTest/DDTSoftNetTest.dsc -b DEBUG
echo "=== Build OK ==="
echo "Output: Build/DDTSoftNetTest/DEBUG_GCC5/X64/DDTSoftNetTest.efi"
