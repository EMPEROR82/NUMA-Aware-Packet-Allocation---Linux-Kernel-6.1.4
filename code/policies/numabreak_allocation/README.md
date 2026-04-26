# Policy: Numabreak-Based NUMA Allocation

## What It Does

This policy uses **kprobes** to intercept two kernel allocation functions and redirects them based on a **packet-size threshold** called `numabreak`:

| Kprobe | Allocation intercepted | Redirection logic |
|--------|----------------------|-------------------|
| `__alloc_pages` | RX page/frag allocation (DMA buffer) | Always → `dma_nid` |
| `__alloc_skb` | SKB slab allocation | → `slow_nid` (node 1) if `pkt_size <= numabreak`, otherwise no redirect |

The key idea: **small packets** (≤ `numabreak` bytes) are expensive to process and benefit from being placed on a specific NUMA node (`slow_nid`). Large packets follow the default `dma_nid` path. The threshold `numabreak` is a kernel variable exported by the patched `e1000_main.c`.

### Guards

- Page allocations only redirect when `in_rx_alloc == true` or `in_clean_alloc == true`
- SKB allocations only redirect when `in_clean_alloc == true`
- The packet size check for SKBs reads `regs->di` (first argument to `__alloc_skb`, which is the requested size)

---

## Files

```
numabreak_allocation/
├── numabreak_policy.c    # Module source
├── Makefile              # obj-m += numabreak_policy.o
└── build_and_load.sh     # Build + insmod
```

---

## Prerequisites

- Patched kernel (run `prepare.sh` + `change_mode.sh`, build and boot)
- `e1000` driver loaded: `lsmod | grep e1000`
- `CONFIG_KPROBES=y` in kernel config
- The kernel must export `numabreak` and `dma_nid` (done by the patched `e1000_main.c` and `rx_timing.h`)

---

## Build & Load

```bash
cd code/policies/numabreak_allocation/
chmod +x build_and_load.sh
./build_and_load.sh
```

This will:
1. Check that `e1000` is loaded
2. Remove the module if already inserted
3. Run `make clean && make`
4. `insmod numabreak_policy.ko`

**Verify it loaded:**
```bash
dmesg | tail -5
# Should see: numabreak_policy: module loaded
lsmod | grep numabreak_policy
```

---

## How the Policy Works

```
Packet arrives via e1000 NIC
        │
        ▼
__alloc_pages (RX frag/DMA buffer)
  ├─ in_rx_alloc  == true  →  regs->dx = dma_nid
  └─ in_clean_alloc == true → regs->dx = dma_nid

__alloc_skb (SKB slab)
  └─ in_clean_alloc == true AND pkt_size <= numabreak
       → regs->cx = slow_nid  (node 1, configurable at compile time)
     otherwise: no redirect (kernel picks node)
```

### Key variables (from patched kernel)

| Variable | Source | Meaning |
|----------|--------|---------|
| `numabreak` | `e1000_main.c` / `rx_timing.h` | Packet size threshold in bytes |
| `dma_nid` | `e1000_main.c` | NUMA node for DMA page allocations |
| `slow_nid` | `numabreak_policy.c` (static, default `1`) | NUMA node for small-packet SKB allocations |

`slow_nid` is defined as a `static int` inside the module (default = `1`). To change it, edit the source and rebuild:
```c
static int slow_nid = 1;   // change this value
```

---

## No Runtime Sysfs Controls

This module has **no sysfs interface**. The threshold (`numabreak`) and page target (`dma_nid`) are kernel-side variables set by the e1000 driver. `slow_nid` is fixed at module compile time.

To change `slow_nid` at runtime you would need to:
1. Edit `numabreak_policy.c` and set the desired value
2. `sudo rmmod numabreak_policy`
3. `make && sudo insmod numabreak_policy.ko`

---

## Typical Benchmarking Workflow

```bash
# 1. Load module
./build_and_load.sh

# 2. Enable latency collection
echo r | sudo tee /sys/kernel/debug/rx_timing
echo 1 | sudo tee /sys/kernel/debug/rx_timing

# 3. Run iperf3 test from host
python3 script.py --output numabreak_64_p1.csv --size 64 --parallel 1 --duration 120

# 4. Collect latency
sudo cat /sys/kernel/debug/rx_timing > numabreak_64_p1.txt
echo r | sudo tee /sys/kernel/debug/rx_timing

# 5. Repeat for other packet sizes
python3 script.py --output numabreak_1024_p1.csv --size 1024 --parallel 1 --duration 120
sudo cat /sys/kernel/debug/rx_timing > numabreak_1024_p1.txt

# 6. Unload when done
sudo rmmod numabreak_policy
```

**Expected behaviour:** For traffic with `pkt_size <= numabreak`, SKBs land on `slow_nid` (node 1). For larger traffic, the allocation follows `dma_nid` (node 0 by default). This separation aims to reduce cross-NUMA penalties for the network stack processing of small packets.

---

## Unload

```bash
sudo rmmod numabreak_policy
dmesg | tail -3
# Should see: numabreak_policy: module unloaded
```
