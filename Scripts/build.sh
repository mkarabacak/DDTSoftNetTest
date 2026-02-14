#!/bin/bash
# DDTSoftNetTest - Build only
set -e

WORKSPACE_ROOT="/home/mahir-karabacak/workspace"
EDK2_DIR="$WORKSPACE_ROOT/edk2"
DDTSOFT_DIR="$WORKSPACE_ROOT/DDTSoft"

cd "$EDK2_DIR"
source edksetup.sh
export PACKAGES_PATH="$EDK2_DIR:$DDTSOFT_DIR"

echo "=== Building DDTSoftNetTest ==="
build -a X64 -t GCC5 -p DDTSoftNetTest/DDTSoftNetTest.dsc -b DEBUG
echo "=== Build OK ==="
echo "Output: $EDK2_DIR/Build/DDTSoftNetTest/DEBUG_GCC5/X64/DDTSoftNetTest.efi"
