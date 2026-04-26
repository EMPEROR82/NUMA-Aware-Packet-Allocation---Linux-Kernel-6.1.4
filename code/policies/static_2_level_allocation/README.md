# Policy: Static 2-Level NUMA Allocation

## What It Does

This policy intercepts two kernel allocation functions using **kprobes** and redirects them to a user-chosen NUMA node:

| Kprobe | Allocation intercepted | Controlled by |
|--------|----------------------|---------------|
| `__alloc_pages` | RX page/frag allocation (DMA buffer filled by NIC) | `enable` + `nid` |
| `__alloc_skb` | SKB slab allocation (socket buffer header + data) | `force_small_enable` + `small_nid` |

The two knobs are **independent** — you can force just the pages, just the SKBs, both, or neither.

The policy is **static**: you pick the target node once via sysfs, and every allocation goes there until you change it or unload the module.

### Guards

- Page allocations are only redirected when `in_rx_alloc == true` (set by the patched `e1000_main.c` during the RX frag allocation path).
- SKB allocations are only redirected when `in_clean_alloc == true` (set by `e1000_main.c` during `e1000_clean_rx_irq`).

Both per-CPU flags are exported by the patched e1000 driver and ensure only RX-path allocations are affected.

---

## Files

```
static_2_level_allocation/
├── static_allocation_policy.c   # Module source
├── Makefile                     # obj-m += static_allocation_policy.o
├── build_and_load.sh            # Build + insmod + sysfs defaults
└── set_node.sh                  # Configure NUMA nodes via sysfs
```

---

## Prerequisites

- Patched kernel (run `prepare.sh` + `change_mode.sh` first, then build and boot)
- `e1000` driver loaded: `lsmod | grep e1000`
- `CONFIG_KPROBES=y` and `CONFIG_DEBUG_FS=y` in kernel config

---

## Build & Load

```bash
cd code/policies/static_2_level_allocation/
chmod +x build_and_load.sh
./build_and_load.sh
```

This will:
1. Check that `e1000` is loaded
2. Remove the module if already inserted
3. Run `make clean && make`
4. `insmod static_allocation_policy.ko`
5. Set all sysfs knobs to 0 (disabled) as defaults

**Verify it loaded:**
```bash
dmesg | tail -5
# Should see: force_node_static: module loaded
lsmod | grep static_allocation_policy
```

---

## Sysfs Interface

All controls live under `/sys/kernel/numa_force_static/`:

| File | Values | Effect |
|------|--------|--------|
| `enable` | `0` / `1` | Toggle NUMA forcing for `__alloc_pages` (RX frag/DMA buffers) |
| `nid` | `0` / `1` | Target NUMA node for page allocations |
| `force_small_enable` | `0` / `1` | Toggle NUMA forcing for `__alloc_skb` (SKB allocations) |
| `small_nid` | `0` / `1` | Target NUMA node for SKB allocations |

**Read current values:**
```bash
cat /sys/kernel/numa_force_static/enable
cat /sys/kernel/numa_force_static/nid
cat /sys/kernel/numa_force_static/force_small_enable
cat /sys/kernel/numa_force_static/small_nid
```

**Write directly:**
```bash
echo 1 | sudo tee /sys/kernel/numa_force_static/enable
echo 0 | sudo tee /sys/kernel/numa_force_static/nid
```

---

## Using `set_node.sh`

```bash
chmod +x set_node.sh

# Usage:
./set_node.sh <page_enable> <page_nid> [<skb_enable> <skb_nid>]
```

| Example | Effect |
|---------|--------|
| `./set_node.sh 1 0` | Force page (DMA) allocations → node 0 |
| `./set_node.sh 1 1` | Force page allocations → node 1 |
| `./set_node.sh 1 0 1 0` | Force pages + SKBs → node 0 |
| `./set_node.sh 1 1 1 1` | Force pages + SKBs → node 1 |
| `./set_node.sh 0 0 0 0` | Disable all forcing (back to kernel default) |

The script prints current values after applying settings.

---

## Typical Benchmarking Workflow

```bash
# 1. Load module with defaults (all disabled)
./build_and_load.sh

# 2. Enable latency collection on VM
echo r | sudo tee /sys/kernel/debug/rx_timing
echo 1 | sudo tee /sys/kernel/debug/rx_timing

# 3. Apply policy — e.g. force everything to node 0
./set_node.sh 1 0 1 0

# 4. Run iperf3 test from host
python3 script.py --output static_node0_64_p1.csv --size 64 --parallel 1 --duration 120

# 5. Collect latency snapshot
sudo cat /sys/kernel/debug/rx_timing > static_node0_64_p1.txt
echo r | sudo tee /sys/kernel/debug/rx_timing

# 6. Change policy — e.g. force to node 1
./set_node.sh 1 1 1 1

# 7. Repeat test
python3 script.py --output static_node1_64_p1.csv --size 64 --parallel 1 --duration 120
```

---

## Unload

```bash
sudo rmmod static_allocation_policy
dmesg | tail -3
# Should see: force_node_static: module unloaded
```
