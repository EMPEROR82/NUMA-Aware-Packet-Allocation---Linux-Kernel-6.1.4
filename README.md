# NUMA-Aware Packet Allocation — Linux Kernel 6.1.4

**CS614 Course Project** | NUMA-aware placement policy for network packets in the Linux kernel RX path (e1000 driver).

---

## Directory Structure

```
.
├── README.md
├── code/
│   ├── prepare.sh                              # Phase 1: copy common kernel files into kernel tree
│   ├── change_mode.sh                          # Phase 2: inject policy e1000_main.c + build
│   ├── kernel_files/
│   │   ├── common_kernel_files/                # Patched kernel sources (shared across all policies)
│   │   │   ├── rx_timing.h                    # Latency framework header (structs, per-CPU decls)
│   │   │   ├── rx_timing.c                    # debugfs /sys/kernel/debug/rx_timing impl
│   │   │   ├── e1000_main.c                   # Base patched e1000 driver
│   │   │   ├── dev.c                          # Patched net/core/dev.c
│   │   │   ├── gro.c                          # Patched net/core/gro.c
│   │   │   ├── ip_input.c                     # Patched net/ipv4/ip_input.c
│   │   │   ├── tcp_ipv4.c                     # Patched net/ipv4/tcp_ipv4.c
│   │   │   ├── tcp_input.c                    # Patched net/ipv4/tcp_input.c
│   │   │   ├── tcp.c                          # Patched net/ipv4/tcp.c
│   │   │   └── Makefile                       # net/core Makefile (adds rx_timing.o)
│   │   ├── static_policy/
│   │   │   └── e1000_main.c                   # Static 2-level policy variant of e1000_main.c
│   │   ├── proportion_policy/
│   │   │   └── e1000_main.c                   # Proportion-based dynamic policy variant
│   │   └── numabreak_policy/
│   │       └── e1000_main.c                   # Numabreak threshold policy variant
│   ├── policies/
│   │   ├── static_2_level_allocation/          # → static_allocation_policy.ko
│   │   │   ├── static_allocation_policy.c     # kprobes on __alloc_pages + __alloc_skb
│   │   │   ├── Makefile
│   │   │   ├── build_and_load.sh              # Build, insmod, set sysfs defaults
│   │   │   ├── set_node.sh                    # Configure NUMA nodes via sysfs knobs
│   │   │   └── README.md                      # Policy usage guide
│   │   ├── proportion_based_dynamic_allocation/  # → proportion_based_policy.ko
│   │   │   ├── proportion_based_policy.c      # Proportion-based kprobe module source
│   │   │   ├── Makefile
│   │   │   ├── build_and_load.sh              # Build + insmod
│   │   │   └── README.md                      # Policy usage guide
│   │   └── numabreak_allocation/              # → numabreak_policy.ko
│   │       ├── numabreak_policy.c             # kprobe: SKBs ≤ numabreak → slow_nid
│   │       ├── Makefile
│   │       ├── build_and_load.sh              # Build + insmod
│   │       └── README.md                      # Policy usage guide
│   └── testing_scripts/
│       ├── benchmarking/
│       │   ├── BENCHMARKING.md                # Full benchmarking workflow guide
│       │   ├── client_side/
│       │   │   ├── script.py                  # iperf3 sweep runner (CSV output)
│       │   │   ├── randomised_script.py       # Random packet-size range sweep
│       │   │   ├── analyse_results_gen.py     # Bar chart: any CSV, filename as X-axis
│       │   │   └── analyse.py                 # Line chart: node_XX_SIZE_P.csv naming
│       │   └── server_side/
│       │       ├── analyse_results_gen.py     # Bar chart: any TXT, filename as X-axis
│       │       └── analyse.py                 # Line chart: node_XX_SIZE_P.txt naming
│       ├── dma_allocation/
│       │   ├── dma_alloc_test.c               # kprobe: logs NUMA node at napi_build_skb
│       │   └── Makefile
│       └── network_allocation/
│           ├── network_alloc_test.c           # kprobe: logs NUMA node at eth_type_trans
│           └── Makefile
└── results/
    ├── static_allocation/
    │   ├── latency_results/
    │   └── throughput_results/
    ├── proportion_based_dynamic_allocation/
    │   ├── latency_results/
    │   └── throughput_results/
    └── numabreak_allocation/
        ├── data/
        └── rsyd_data.zip
```

---

## System Requirements

| Component | Requirement |
|-----------|-------------|
| **OS** | Ubuntu 22.04 LTS (x86_64) |
| **CPU** | x86-64, 2 NUMA nodes, ≥ 4 cores recommended |
| **NIC** | Intel e1000 (QEMU/KVM default NIC) |
| **Extra** | Host machine to run iperf3 client; VM as receiver |

### Software Dependencies

```bash
sudo apt update
sudo apt install -y build-essential libncurses-dev bison flex \
    libssl-dev libelf-dev bc git iperf3 python3-pip
pip3 install matplotlib pandas
```

---

## Kernel Setup

### 1. Download Linux 6.1.4

```bash
wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.1.4.tar.xz
tar -xf linux-6.1.4.tar.xz
cd linux-6.1.4
```

> **Human time:** ~5 min | **Download time:** depends on bandwidth (~130 MB)

### 2. Patch the Kernel (Phase 1 — Common Files)

Run from the `code/` directory of this repo:

```bash
cd code/
chmod +x prepare.sh
./prepare.sh
# Enter absolute path to linux-6.1.4 when prompted
```

This copies the following patched files into the kernel tree:

| Source File | Kernel Destination |
|-------------|-------------------|
| `rx_timing.h` | `include/net/rx_timing.h` |
| `rx_timing.c` | `net/core/rx_timing.c` |
| `Makefile` | `net/core/Makefile` |
| `gro.c` | `net/core/gro.c` |
| `dev.c` | `net/core/dev.c` |
| `ip_input.c` | `net/ipv4/ip_input.c` |
| `tcp_ipv4.c` | `net/ipv4/tcp_ipv4.c` |
| `tcp_input.c` | `net/ipv4/tcp_input.c` |
| `tcp.c` | `net/ipv4/tcp.c` |
| `e1000_main.c` | `drivers/net/ethernet/intel/e1000/e1000_main.c` |

### 3. Inject a NUMA Policy (Phase 2 — Policy Selection)

```bash
chmod +x change_mode.sh
./change_mode.sh
# Select kernel path, then choose a policy:
#   static_policy / proportion_policy / numabreak_policy
# Optionally start the build immediately
```

`change_mode.sh` copies the chosen policy's `e1000_main.c` into the kernel tree, replacing the base version. This selects which NUMA decision logic runs in the driver.

### 4. Configure the Kernel

```bash
cd /path/to/linux-6.1.4
cp /boot/config-$(uname -r) .config
make olddefconfig
```

#### 4a. Set e1000 as a Loadable Module (`[M]`)

> **This is required.** The policy kprobe modules import symbols exported by the patched e1000 driver (`in_rx_alloc`, `in_clean_alloc`, `dma_nid`, `numabreak`). These symbols are only available to other modules when e1000 is built as `[M]`. If built-in (`[*]`), the policy modules will fail to load with unresolved symbol errors.

**Option A — scriptable (recommended):**

```bash
cd /path/to/linux-6.1.4

# Set e1000 as a loadable module
scripts/config --module CONFIG_E1000

# Reconcile any dependency changes
make olddefconfig
```

**Option B — interactive menuconfig:**

```bash
make menuconfig
```

Navigate to:
```
Device Drivers
  └── Network device support
        └── Ethernet driver support
              └── Intel devices
                    └── Intel(R) PRO/1000 Gigabit Ethernet support
                          → change [*] or [ ] to [M]   (CONFIG_E1000)
```

#### 4b. Build and Install

```bash
make -j$(nproc)
sudo make modules_install
sudo make install
sudo reboot
```

> **Compute time:** 30–90 min depending on CPU cores.

---

## Features

### A. Per-Stage Latency Instrumentation (`rx_timing`)

Timestamps are inserted at 11 stages of the RX path inside the patched kernel sources:

| Stage | Location |
|-------|----------|
| `e1000_alloc_frag` | e1000 RX frag allocation |
| `napi_build_skb` | SKB construction |
| `copybreak_alloc` | Copybreak small-packet copy |
| `prefetch_gap` | Prefetch window |
| `napi_gro_receive` | GRO entry |
| `__netif_receive_core` | Core stack dispatch |
| `ip_rcv_core` | IP layer |
| `tcp_v4_rcv` | TCP entry |
| `tcp_queue_rcv` | Socket enqueue |
| `copy_to_user` | User-space copy |
| `e2e_softirq` | End-to-end softirq |

**Controlling the debugfs interface:**

```bash
# Enable timing collection
echo 1 | sudo tee /sys/kernel/debug/rx_timing

# Read results (count / avg_ns / max_ns per stage)
cat /sys/kernel/debug/rx_timing

# Reset counters
echo r | sudo tee /sys/kernel/debug/rx_timing

# Disable
echo 0 | sudo tee /sys/kernel/debug/rx_timing
```

**Output format:**
```
stage                     count        avg_ns        max_ns
-----                     -----        ------        ------
e1000_alloc_frag        2012547           174         19152
napi_build_skb          2012547           312         25600
...
enabled: 1
```

---

## NUMA Policies

> ⚠️ **Load only ONE policy module at a time.** Loading multiple modules simultaneously is untested and will cause conflicts between kprobes.

### Policy 1 — Static 2-Level Allocation (`static_2_level_allocation`)

Uses kprobes on `__alloc_pages` and `__alloc_skb` to redirect allocations to a **user-specified NUMA node**. Two independent knobs separately control RX page allocations (DMA buffers) and SKB allocations (socket buffers). Fully controllable at runtime via sysfs.

**Kernel driver:** `kernel_files/static_policy/e1000_main.c`  
**Module:** `static_allocation_policy.ko`

```bash
cd code/policies/static_2_level_allocation/

# Build and insert (checks e1000 is loaded)
chmod +x build_and_load.sh && ./build_and_load.sh

# Configure NUMA node
chmod +x set_node.sh
./set_node.sh <page_enable> <page_nid> [<skb_enable> <skb_nid>]

# Examples:
./set_node.sh 1 0          # Force RX pages → node 0
./set_node.sh 1 1          # Force RX pages → node 1
./set_node.sh 1 0 1 0      # Force pages + SKBs → node 0
./set_node.sh 0 0 0 0      # Disable all forcing

# Remove
sudo rmmod static_allocation_policy
```

**Sysfs knobs at `/sys/kernel/numa_force_static/`:**

| File | Effect |
|------|--------|
| `enable` | Toggle page alloc forcing (0/1) |
| `nid` | Target node for page (DMA) allocations |
| `force_small_enable` | Toggle SKB alloc forcing (0/1) |
| `small_nid` | Target node for SKB allocations |

> See `code/policies/static_2_level_allocation/README.md` for full details.

---

### Policy 2 — Proportion-Based Dynamic Allocation (`proportion_based_dynamic_allocation`)

Automatically steers all RX allocations to the NUMA node indicated by `dma_nid`, which is updated by the patched `e1000_main.c` based on per-CPU packet traffic proportions (small vs. large packets). **No sysfs interface** — the policy is fully automatic once loaded.

**Kernel driver:** `kernel_files/proportion_policy/e1000_main.c`  
**Module:** `proportion_based_policy.ko`

```bash
cd code/policies/proportion_based_dynamic_allocation/

# Build and insert (checks e1000 is loaded)
chmod +x build_and_load.sh && ./build_and_load.sh

# Remove
sudo rmmod proportion_based_policy
```

> See `code/policies/proportion_based_dynamic_allocation/README.md` for full details.

---

### Policy 3 — Numabreak Threshold Allocation (`numabreak_allocation`)

Splits allocations based on a **packet-size threshold** (`numabreak`): SKBs for small packets (≤ `numabreak` bytes) are redirected to `slow_nid` (node 1); large packet allocations follow the default `dma_nid` path. **No sysfs interface** — `slow_nid` is a compile-time constant (default: 1).

**Kernel driver:** `kernel_files/numabreak_policy/e1000_main.c`  
**Module:** `numabreak_policy.ko`

```bash
cd code/policies/numabreak_allocation/

# Build and insert (checks e1000 is loaded)
chmod +x build_and_load.sh && ./build_and_load.sh

# Remove
sudo rmmod numabreak_policy
```

> See `code/policies/numabreak_allocation/README.md` for full details.

---

## Benchmarking

> 📄 **Full workflow:** `code/testing_scripts/benchmarking/BENCHMARKING.md`

### Setup

```bash
# VM (server): configure IP and start iperf3
sudo ip addr add 192.168.100.2/24 dev eth0
sudo ip link set eth0 up
iperf3 -s

# Reset and enable latency counters before each run
echo r | sudo tee /sys/kernel/debug/rx_timing
echo 1 | sudo tee /sys/kernel/debug/rx_timing
```

### Running Tests (Host / Client Side)

`script.py` supports `--size`, `--parallel`, `--runs`, `--duration` flags (all optional with defaults):

```bash
cd code/testing_scripts/benchmarking/client_side/

# Full sweep: all default sizes (64–1024 B), p=1 and p=2
python3 script.py --output baseline.csv

# Specific config: 64 B, 1 stream, 3 runs × 120 s
python3 script.py --output static_node0_64_p1.csv \
    --size 64 --parallel 1 --runs 3 --duration 120

# Randomised packet-size sweep (range-based, fixed seed)
python3 randomised_script.py --min-size 64 --max-size 1024 --runs 20 -o rand_baseline.csv
```

### Collecting Latency (VM Side)

```bash
# After test completes, snapshot latency data
sudo cat /sys/kernel/debug/rx_timing > baseline_64_p1.txt

# Reset for next run
echo r | sudo tee /sys/kernel/debug/rx_timing
```

### Generating Plots

Two analysis scripts on each side — see `BENCHMARKING.md §6` for full details.

**Quick bar charts (any filenames, X-axis = filename):**
```bash
# Throughput (client side)
python3 analyse_results_gen.py --dir . --out throughput
# → throughput_bar.png

# Latency (server side)
python3 analyse_results_gen.py --dir . --out latency_plots
# → latency_plots/<stage>.png + latency_plots/combined_latency.png
```

**Structured line charts (requires `node_XX_SIZE_P` filename format):**
```bash
# Throughput (client side)
python3 analyse.py
# → throughput_comparison_p1.png, throughput_comparison_p2.png

# Latency (server side)
python3 analyse.py
# → latency_plots/p_1/<stage>.png, combined_p_1.png, ...
```

---

## Experiment Table

| # | Experiment | Module | Parameters | Script | Est. Runtime |
|---|-----------|--------|------------|--------|-------------|
| 1 | Baseline (no policy) | none | pkt: 64–1024 B, P=1,2 | `script.py` | ~20 min |
| 2 | Static → node 0 (pages + SKBs) | `static_allocation_policy.ko` | `./set_node.sh 1 0 1 0` | `script.py` | ~20 min |
| 3 | Static → node 1 (pages + SKBs) | `static_allocation_policy.ko` | `./set_node.sh 1 1 1 1` | `script.py` | ~20 min |
| 4 | Static → node 0 (pages only) | `static_allocation_policy.ko` | `./set_node.sh 1 0 0 0` | `script.py` | ~20 min |
| 5 | Proportion-based dynamic | `proportion_based_policy.ko` | auto (no sysfs) | `script.py` | ~20 min |
| 6 | Numabreak threshold | `numabreak_policy.ko` | auto (no sysfs) | `script.py` | ~20 min |
| 7 | Randomised size sweep | any policy | `--min-size 64 --max-size 1024` | `randomised_script.py` | ~5 min |
| 8 | DMA allocation node check | `dma_alloc_test` | — | `dmesg` | 2 min |
| 9 | SKB data node check | `network_alloc_test` | — | `dmesg` | 2 min |

**Expected outcome:** Static policies reduce cross-NUMA allocation latency for `e1000_alloc_frag` and `napi_build_skb` stages. Proportion-based and numabreak policies adapt node selection automatically based on traffic characteristics.

---

## Features / Functionalities Supported

> **For a detailed breakdown of all features, test scenarios, parameters (packet sizes), objectives, expected outcomes, and observed findings, please refer to the project report submitted alongside this artifact.**

---

## Allocation Verification Modules

These standalone kprobe modules verify which NUMA node allocations actually land on:

```bash
# DMA / napi_build_skb node check
cd code/testing_scripts/dma_allocation/
make && sudo insmod dma_alloc_test.ko
dmesg | grep napi_build_skb   # prints NUMA node of each RX frag alloc
sudo rmmod dma_alloc_test

# SKB data (eth_type_trans) node check
cd code/testing_scripts/network_allocation/
make && sudo insmod network_alloc_test.ko
dmesg | grep eth_type_trans    # prints node id every ~1000 packets (throttled)
sudo rmmod network_alloc_test
```

---

## Getting Started (< 30 min)

1. Download and extract Linux 6.1.4 (link above)
2. `cd code/ && ./prepare.sh` → enter kernel path
3. `./change_mode.sh` → select `static_policy`; optionally start build
4. Set `CONFIG_E1000=m`: `scripts/config --module CONFIG_E1000 && make olddefconfig`
5. `make -j$(nproc) && sudo make modules_install && sudo make install` then reboot
6. Verify e1000 is loaded: `lsmod | grep e1000`
7. `cd policies/static_2_level_allocation && ./build_and_load.sh`
   → builds and inserts **`static_allocation_policy.ko`**
8. Force node 0: `./set_node.sh 1 0 1 0`
9. Enable latency: `echo 1 | sudo tee /sys/kernel/debug/rx_timing`
10. Send traffic from host: `python3 script.py --output test.csv --size 64 --parallel 1 --duration 30`
11. Read latency: `cat /sys/kernel/debug/rx_timing`

---

## Assumptions & Unsupported Features

- **x86-64 only** — kprobe handlers use `regs->dx` / `regs->cx` (System V ABI). ARM64 not supported.
- **e1000 driver only** — policy hooks are inside `e1000_clean_rx_irq`. Other NICs are not patched.
- **QEMU/KVM recommended** — physical multi-socket NUMA machines also work but require BIOS NUMA enabled.
- **Only one policy module at a time** — simultaneous loading of multiple kprobe modules is untested.
- No known persistent crashes or deadlocks. Occasional `dmesg` warnings about kprobe re-registration if module is inserted without removing first (handled by `build_and_load.sh`).
