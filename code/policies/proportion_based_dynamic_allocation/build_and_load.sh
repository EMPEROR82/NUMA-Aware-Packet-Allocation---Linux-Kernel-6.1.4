#!/bin/bash

set -e

MODULE="force_node_proportion"

echo "[*] Checking e1000 is loaded (required)..."
if ! lsmod | grep -q "^e1000 "; then
    echo "[!] ERROR: e1000 module is not loaded."
    echo "    force_node_proportion requires e1000 (exports in_rx_alloc, in_clean_alloc, dma_nid)."
    echo "    Load it first: sudo modprobe e1000"
    exit 1
fi
echo "[+] e1000 is loaded"

echo "[*] Removing ${MODULE} (if loaded)..."
if lsmod | grep -q "^${MODULE} "; then
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

echo ""
echo "[+] Done. Module loaded successfully."
echo "    Verify: dmesg | tail -5"
echo "    Remove: sudo rmmod ${MODULE}"
