#!/bin/bash
# build_and_load.sh — Build and insert final_probe.ko
# Usage: ./build_and_load.sh

set -e

MODULE="final_probe"
SYSFS_DIR="/sys/kernel/numa_force"

echo "[*] Checking e1000 is loaded (required)..."
if ! lsmod | grep -q "^e1000 "; then
    echo "[!] ERROR: e1000 module is not loaded."
    echo "    final_probe requires e1000 to resolve e1000_clean_rx_irq."
    echo "    Load it first: sudo modprobe e1000"
    exit 1
fi
echo "[+] e1000 is loaded"

echo "[*] Removing ${MODULE} (if loaded)..."
if lsmod | grep -q "^${MODULE}"; then
    sudo rmmod ${MODULE}
    echo "[+] Module removed"
else
    echo "[i] Module not loaded"
fi

echo "[*] Building module..."
make clean
make

echo "[*] Inserting module..."
sudo insmod ${MODULE}.ko

echo "[*] Waiting for sysfs..."
sleep 1

if [ ! -d "${SYSFS_DIR}" ]; then
    echo "[!] Sysfs directory not found!"
    echo "    Check dmesg for errors: dmesg | tail -20"
    exit 1
fi

echo "[*] Setting defaults (all forcing disabled)..."
echo 0 | sudo tee ${SYSFS_DIR}/enable > /dev/null
echo 0 | sudo tee ${SYSFS_DIR}/nid > /dev/null
echo 0 | sudo tee ${SYSFS_DIR}/force_small_enable > /dev/null
echo 0 | sudo tee ${SYSFS_DIR}/small_nid > /dev/null

echo ""
echo "[+] Done. Module loaded successfully."
echo "    Verify: dmesg | tail -5"
echo ""
echo "    Sysfs controls at ${SYSFS_DIR}/:"
echo "      enable             — toggle page alloc NUMA forcing (0/1)"
echo "      nid                — target node for page allocs"
echo "      force_small_enable — toggle SKB alloc NUMA forcing (0/1)"
echo "      small_nid          — target node for SKB allocs"

