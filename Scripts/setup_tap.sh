#!/bin/bash
# DDTSoftNetTest - TAP interface setup for Companion testing
# Kullanim: sudo ./setup_tap.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "=== TAP Interface Setup ==="

# Mevcut tap0 varsa sil
sudo ip link show tap0 &>/dev/null && sudo ip link delete tap0 || true

# Yeni tap0 olustur
sudo ip tuntap add dev tap0 mode tap user $USER
sudo ip addr add 192.168.100.1/24 dev tap0
sudo ip link set tap0 up

echo "tap0 ready: 192.168.100.1/24"
echo ""
echo "=== Simdi iki terminal ac ==="
echo ""
echo "Terminal 1 - Companion:"
echo "  cd $PROJECT_DIR/Companion"
echo "  sudo python3 companion.py -i tap0 --ip 192.168.100.1"
echo ""
echo "Terminal 2 - QEMU:"
echo "  $SCRIPT_DIR/run_with_tap.sh"
