# Policy: Proportion-Based Dynamic NUMA Allocation

## What It Does

This policy intercepts two kernel allocation functions using **kprobes** and redirects them to the NUMA node indicated by the `dma_nid` kernel variable, which is exported by the patched `e1000_main.c`.

| Kprobe | Allocation intercepted | Redirection target |
|--------|----------------------|--------------------|
| `__alloc_pages` | RX page/frag allocation (DMA buffer) | `dma_nid` |
| `__alloc_skb` | SKB slab allocation | `dma_nid` |

Unlike the static policy, the node selection here is driven by `dma_nid` — a kernel-side variable that can be updated dynamically based on packet traffic proportions (small vs. large packets) by the e1000 driver's counter logic.

There is **no sysfs interface** in this module. The policy is fully automatic once loaded — it reads `dma_nid` on every allocation and redirects accordingly.

### Guards

- Page allocations only redirect when `in_rx_alloc == true`
- SKB allocations only redirect when `in_clean_alloc == true`

Both per-CPU flags are set/cleared by the patched `e1000_clean_rx_irq()` in `e1000_main.c`.

---

## Files

```
proportion_based_dynamic_allocation/
├── proportion_based_policy.c   # Module source
├── Makefile                    # obj-m += proportion_based_policy.o
├── build_and_load.sh           # Build + insmod
└── set_node.sh                 # (inherited — not used by this module)
```

> `set_node.sh` is present but has no effect for this policy since there is no sysfs interface.

---

## Prerequisites

- Patched kernel (run `prepare.sh` + `change_mode.sh` selecting `policy_2`, build and boot)
- `e1000` driver loaded: `lsmod | grep e1000`
- `CONFIG_KPROBES=y` in kernel config

---

## Build & Load

```bash
cd code/policies/proportion_based_dynamic_allocation/
chmod +x build_and_load.sh
./build_and_load.sh
```

This will:
1. Check that `e1000` is loaded
2. Remove the module if already inserted
3. Run `make clean && make`
4. `insmod proportion_based_policy.ko`

**Verify it loaded:**
```bash
dmesg | tail -5
# Should see: force_node_proportion: module loaded
lsmod | grep proportion_based_policy
```

---

## How the Policy Works

The `dma_nid` variable is exported by the patched kernel and updated by `e1000_main.c` based on per-CPU packet counters (`packet_counter.nr_small_packets` vs `packet_counter.nr_big_packets`):

- If the CPU has seen **more small packets** than large → `dma_nid = 1`
- Otherwise → `dma_nid = 0`

Counters decay every `POLICY_RESET_LIMIT` (8192) packets by right-shifting `REDUCE_RATIO` (9) bits to prevent stale history from locking in a node permanently.

This module simply reads `dma_nid` at allocation time — all the decision logic lives in the kernel driver.

---

## No Runtime Controls

This module has **no sysfs knobs**. Once loaded, it is always active. To stop it:

```bash
sudo rmmod proportion_based_policy
```

To temporarily pause the effect without unloading, you would need to modify and reload the driver with the `in_rx_alloc` / `in_clean_alloc` guards disabled — which is not supported directly.

---

## Typical Benchmarking Workflow

```bash
# 1. Load module
./build_and_load.sh

# 2. Enable latency collection
echo r | sudo tee /sys/kernel/debug/rx_timing
echo 1 | sudo tee /sys/kernel/debug/rx_timing

# 3. Run iperf3 test from host
python3 script.py --output proportion_64_p1.csv --size 64 --parallel 1 --duration 120

# 4. Collect latency snapshot
sudo cat /sys/kernel/debug/rx_timing > proportion_64_p1.txt
echo r | sudo tee /sys/kernel/debug/rx_timing

# 5. Unload when done
sudo rmmod proportion_based_policy
```

---

## Unload

```bash
sudo rmmod proportion_based_policy
dmesg | tail -3
# Should see: force_node_proportion: module unloaded
```
