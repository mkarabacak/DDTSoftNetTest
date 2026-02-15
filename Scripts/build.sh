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

RELEASE_DIR="$DDTSOFT_DIR/DDTSoftNetTest/Release"
mkdir -p "$RELEASE_DIR"
cp Build/DDTSoftNetTest/DEBUG_GCC5/X64/DDTSoftNetTest.efi "$RELEASE_DIR/"
echo "=== Release updated: $RELEASE_DIR/DDTSoftNetTest.efi ==="
