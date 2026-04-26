#!/bin/bash

set -e

TAP=tap0
HOST_IP=192.168.100.1/24
SSH_PORT=2222

echo "[*] Setting up TAP device..."

# Check if tap exists
if ip link show "$TAP" &>/dev/null; then
    echo "[*] $TAP already exists"
else
    sudo ip tuntap add dev "$TAP" mode tap user $USER
fi

# Assign IP (ignore error if already exists)
sudo ip addr add $HOST_IP dev "$TAP" 2>/dev/null || true

# Bring up tap
sudo ip link set "$TAP" up

echo "[*] TAP ready:"
ip a | grep "$TAP"

echo "[*] Launching VM..."
qemu-system-x86_64 -accel kvm \
-cpu host \
-machine pc,hmat=on \
-m 10G \
-smp 10 \
-drive if=pflash,format=raw,file=./OVMF.fd \
-drive media=disk,format=qcow2,file=efi.qcow2 \
-netdev user,id=nat,hostfwd=tcp::2222-:22 \
-device e1000,netdev=nat \
-netdev tap,id=n1,ifname=tap0,script=no,downscript=no \
-device e1000,netdev=n1 \
-object memory-backend-ram,size=4G,id=m0 \
-object memory-backend-ram,size=6G,id=m1 \
-numa node,nodeid=0,memdev=m0,cpus=0-9 \
-numa node,nodeid=1,memdev=m1 \
-numa hmat-lb,initiator=0,target=0,hierarchy=memory,\
data-type=access-latency,latency=10 \
-numa hmat-lb,initiator=0,target=0,hierarchy=memory,\
data-type=access-bandwidth,bandwidth=10G \
-numa hmat-lb,initiator=0,target=1,hierarchy=memory,\
data-type=access-latency,latency=100 \
-numa hmat-lb,initiator=0,target=1,hierarchy=memory,\
data-type=access-bandwidth,bandwidth=500M
