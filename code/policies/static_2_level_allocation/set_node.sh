#!/bin/bash
set -e

SYSFS_DIR="/sys/kernel/numa_force_static"

if [ "$#" -lt 2 ]; then
    echo "Usage: $0 <page_enable> <page_nid> [<skb_enable> <skb_nid>]"
    echo ""
    echo "Arguments:"
    echo "  page_enable  — 1 to force page alloc NUMA node, 0 to disable"
    echo "  page_nid     — target NUMA node for page allocations"
    echo "  skb_enable   — (optional) 1 to force SKB alloc NUMA node"
    echo "  skb_nid      — (optional) target NUMA node for SKB allocations"
    echo ""
    echo "Examples:"
    echo "  $0 1 0         — Force page allocs to node 0"
    echo "  $0 1 1 1 1     — Force all allocs to node 1"
    echo "  $0 0 0 0 0     — Disable all forcing"
    exit 1
fi

if [ ! -d "${SYSFS_DIR}" ]; then
    echo "[!] Sysfs directory not found: ${SYSFS_DIR}"
    echo "    Is static_allocation_policy module loaded? Run: lsmod | grep static_allocation_policy"
    exit 1
fi

PAGE_ENABLE=$1
PAGE_NID=$2
SKB_ENABLE=${3:-}
SKB_NID=${4:-}

echo "[*] Setting page alloc: enable=${PAGE_ENABLE}, nid=${PAGE_NID}"
echo ${PAGE_ENABLE} | sudo tee ${SYSFS_DIR}/enable > /dev/null
echo ${PAGE_NID}    | sudo tee ${SYSFS_DIR}/nid > /dev/null

if [ -n "${SKB_ENABLE}" ] && [ -n "${SKB_NID}" ]; then
    echo "[*] Setting SKB alloc:  enable=${SKB_ENABLE}, nid=${SKB_NID}"
    echo ${SKB_ENABLE} | sudo tee ${SYSFS_DIR}/force_small_enable > /dev/null
    echo ${SKB_NID}    | sudo tee ${SYSFS_DIR}/small_nid > /dev/null
fi

echo ""
echo "[+] Current settings:"
echo "    enable             = $(cat ${SYSFS_DIR}/enable)"
echo "    nid                = $(cat ${SYSFS_DIR}/nid)"
echo "    force_small_enable = $(cat ${SYSFS_DIR}/force_small_enable)"
echo "    small_nid          = $(cat ${SYSFS_DIR}/small_nid)"
