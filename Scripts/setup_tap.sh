#!/bin/bash
# Companion testi icin tap interface olustur
set -e
sudo ip tuntap add dev tap0 mode tap user $USER
sudo ip addr add 192.168.100.1/24 dev tap0
sudo ip link set tap0 up
echo "tap0 ready: 192.168.100.1/24"
echo "QEMU komutu:"
echo "qemu-system-x86_64 -bios /usr/share/OVMF/OVMF_CODE.fd \\"
echo "  -drive format=raw,file=fat:rw:\$HOME/efi_disk \\"
echo "  -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \\"
echo "  -device e1000,netdev=net0 -m 512M -nographic"
