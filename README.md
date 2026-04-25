# NUMA-Aware Packet Allocation — Linux Kernel 6.1.4

**CS614 Course Project** | NUMA-aware placement policy for network packets in the Linux kernel RX path (e1000 driver).

---

## Directory Structure

```
.
├── code/
│   ├── prepare.sh                          # Phase 1: copy common kernel files
│   ├── change_mode.sh                      # Phase 2: inject policy e1000_main.c + build
│   ├── kernel_files/
│   │   ├── common_kernel_files/            # Patched kernel sources (copy into kernel tree)
│   │   │   ├── rx_timing.h                # Latency framework header (structs, per-CPU decls)
│   │   │   ├── rx_timing.c                # debugfs /sys/kernel/debug/rx_timing impl
│   │   │   ├── e1000_main.c               # Patched e1000 driver (per-CPU flags + counters)
│   │   │   ├── dev.c                      # Patched net/core/dev.c
│   │   │   ├── gro.c                      # Patched net/core/gro.c
│   │   │   ├── ip_input.c                 # Patched net/ipv4/ip_input.c
│   │   │   ├── tcp_ipv4.c                 # Patched net/ipv4/tcp_ipv4.c
│   │   │   ├── tcp_input.c                # Patched net/ipv4/tcp_input.c
│   │   │   ├── tcp.c                      # Patched net/ipv4/tcp.c
│   │   │   └── Makefile                   # net/core Makefile (adds rx_timing.o)
│   │   ├── policy_1/
│   │   │   └── e1000_main.c              # Policy 1 variant of e1000_main.c
│   │   ├── policy_2/
│   │   │   └── e1000_main.c              # Policy 2 variant of e1000_main.c
│   │   └── policy_3/                      # (empty — reserved)
│   ├── policies/
│   │   ├── static_2_level_allocation/     # Static kprobe module → static_allocation_policy.ko
│   │   │   ├── static_allocation_policy.c # kprobes on __alloc_pages + __alloc_skb
│   │   │   ├── Makefile                   # obj-m += static_allocation_policy.o
│   │   │   ├── build_and_load.sh          # Build, insmod, set sysfs defaults
│   │   │   └── set_node.sh               # Configure NUMA node via sysfs knobs
│   │   └── proportion_based_dynamic_allocation/  # Dynamic policy module → final_probe.ko
│   │       ├── dny_dynamic.c              # Proportion-based kprobe module source
│   │       ├── Makefile                   # obj-m += final_probe.o
│   │       └── set_node.sh               # Configure/override via sysfs knobs
│   └── testing_scripts/
│       ├── benchmarking/
│       │   ├── client_side/
│       │   │   ├── script.py              # iperf3 client runner (outputs CSV)
│       │   │   └── analyse_results.py     # Throughput line plots
│       │   └── server_side/
│       │       └── analyse_results.py     # Latency plots from rx_timing text files
│       ├── dma_allocation/
│       │   ├── dma_alloc_test.c           # kprobe: logs NUMA node of napi_build_skb alloc
│       │   └── Makefile
│       └── network_allocation/
│           ├── network_alloc_test.c       # kprobe: logs NUMA node of eth_type_trans SKB
│           └── Makefile
└── results/
    ├── static_allocation/
    │   └── latency_results/
    ├── proportion_based_dynamic_allocation/
    │   ├── latency_results/               # per-stage avg/max latency data + plots
    │   └── throughput_results/            # iperf3 CSV data + throughput plots
    └── free_rambased_dynamic_allocation/  # Data + plots for free-RAM-based policy
```

---

## System Requirements

| Component | Requirement |
|-----------|-------------|
| **OS** | Ubuntu 22.04 LTS (x86_64) |
| **CPU** | x86-64, 2 NUMA nodes, ≥ 4 cores recommended |
| **NIC** | Only one Intel e1000 (QEMU/KVM default; physical e1000 also works) |
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
|-------------|--------------------|
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
# Select kernel path, then choose policy (static_2_level_allocation / proportion_based_dynamic_allocation / free_rambased_dynamic_allocation)
# Optionally start the build immediately
```

### 4. Configure and Build the Kernel

```bash
cd /path/to/linux-6.1.4
cp /boot/config-$(uname -r) .config
make olddefconfig
# Ensure kprobes and debugfs are enabled:
# CONFIG_KPROBES=y, CONFIG_DEBUG_FS=y
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

> ⚠️ **Load only ONE policy module at a time.** Loading multiple modules simultaneously is untested and will cause conflicts.

### Policy 1 — Static 2-Level Allocation (`static_2_level_allocation`)

Uses kprobes on `__alloc_pages` and `__alloc_skb` to redirect allocations to a user-specified NUMA node. Two independent knobs control **RX page allocations** (DMA buffers) and **SKB allocations** (socket buffers) separately.

```bash
cd code/policies/static_2_level_allocation/

# Build and insert module (checks e1000 is loaded first)
# Produces: static_allocation_policy.ko
chmod +x build_and_load.sh
./build_and_load.sh

# Set NUMA node for allocations
chmod +x set_node.sh
./set_node.sh <page_enable> <page_nid> [<skb_enable> <skb_nid>]

# Examples:
./set_node.sh 1 0          # Force RX pages → node 0
./set_node.sh 1 1          # Force RX pages → node 1
./set_node.sh 1 0 1 0      # Force pages + SKBs → node 0
./set_node.sh 0 0 0 0      # Disable all forcing
```

**Sysfs knobs** (`/sys/kernel/numa_force/`):
- `enable` — toggle page alloc forcing (0/1)
- `nid` — target node for page allocations
- `force_small_enable` — toggle SKB alloc forcing (0/1)
- `small_nid` — target node for SKB allocations

**Remove module:**
```bash
sudo rmmod static_allocation_policy
# Verify: lsmod | grep static_allocation_policy
```

---

### Policy 2 — Proportion-Based Dynamic Allocation (`proportion_based_dynamic_allocation`)

Automatically steers allocations based on the **ratio of small vs. large packets** seen per CPU. If more small packets → allocate on node 1; otherwise → node 0. The policy runs entirely in the kprobe handlers using per-CPU packet counters maintained by the patched `e1000_main.c`.

The policy resets counters every `POLICY_RESET_LIMIT` (8192) packets and decays counts by right-shifting `REDUCE_RATIO` (9) bits to avoid stale history.

```bash
cd code/policies/proportion_based_dynamic_allocation/

# Build — produces final_probe.ko (see Makefile: obj-m += final_probe.o)
make
sudo insmod final_probe.ko

# Enable/disable the dynamic policy at runtime via sysfs
echo 1 | sudo tee /sys/kernel/numa_force/dny_dynamic   # enable
echo 0 | sudo tee /sys/kernel/numa_force/dny_dynamic   # disable

# Optional: static override on top of dynamic via set_node.sh
chmod +x set_node.sh
./set_node.sh 1 0      # force pages to node 0 regardless of packet ratio

# Remove module
sudo rmmod final_probe
# Verify: lsmod | grep final_probe
```

---

## Benchmarking

### Setup

```bash
# On VM (server side) — configure IP and start iperf3
# (set up tap/bridge networking between host and VM first)
ip addr add 192.168.100.2/24 dev eth0
ip link set eth0 up
iperf3 -s   # listens on port 5201
```

### Running Tests (Host / Client Side)

```bash
cd code/testing_scripts/benchmarking/client_side/

# Parameters: --size (bytes), --parallel (streams), --output (csv file)
python3 script.py --size 64   --parallel 1 --output node_00_64_1.csv
python3 script.py --size 128  --parallel 1 --output node_00_128_1.csv
python3 script.py --size 1024 --parallel 2 --output node_00_1024_2.csv
```

Output file naming convention: `node_<cfg>_<pktsize>_<streams>.csv`  
where `cfg` = `00` (baseline), `10` (pages forced), `11` (pages+SKBs forced).

> **Test duration:** 120 sec per run (configurable via `DURATION` in `script.py`).  
> **Server IP:** edit `SERVER = "192.168.100.2"` in `script.py` to match your setup.

### Generating Plots

**Throughput plots (client side):**
```bash
cd code/testing_scripts/benchmarking/client_side/
python3 analyse_results.py
# Output: throughput_comparison_p1.png, throughput_comparison_p2.png
```

**Latency plots (server side — from rx_timing data):**
```bash
cd code/testing_scripts/benchmarking/server_side/
# Place rx_timing output files named node_<cfg>_<pktsize>_<streams>.txt here
python3 analyse_results.py
# Output: latency_plots/p_1/, latency_plots/p_2/, combined_p_1.png, combined_p_2.png
```

---

## Experiment Table

| # | Experiment | Module | Parameters | Script | Est. Runtime |
|---|-----------|--------|------------|--------|-------------|
| 1 | Baseline latency (no policy) | none | pkt: 64–1024 B, P=1,2 | `script.py` | ~20 min |
| 2 | Static policy — node 0 | `static_allocation_policy.ko` | `./set_node.sh 1 0 1 0` | `script.py` | ~20 min |
| 3 | Static policy — node 1 | `static_allocation_policy.ko` | `./set_node.sh 1 1 1 1` | `script.py` | ~20 min |
| 4 | Proportion-based dynamic | `final_probe.ko` | `echo 1 > .../dny_dynamic` | `script.py` | ~20 min |
| 5 | DMA allocation node check | `dma_alloc_test` | — | `dmesg` | 2 min |
| 6 | SKB data node check | `network_alloc_test` | — | `dmesg` | 2 min |

**Expected outcome:** Policies 2 and 3 reduce cross-NUMA allocation latency for `e1000_alloc_frag` and `napi_build_skb` stages. Dynamic policy (Policy 4) tracks packet mix and adapts node selection without manual tuning.

---

## Allocation Verification Modules

These standalone kprobe modules are in `testing_scripts/` and let you verify which NUMA node allocations land on:

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
3. `./change_mode.sh` → select `policy_1`; optionally start build
4. Build kernel (`make -j$(nproc)`) and boot into new kernel
5. Verify e1000 is loaded: `lsmod | grep e1000`
6. `cd policies/static_2_level_allocation && ./build_and_load.sh`  
   → builds and inserts **`static_allocation_policy.ko`**
7. Force node 0: `./set_node.sh 1 0 1 0`
8. Enable latency: `echo 1 | sudo tee /sys/kernel/debug/rx_timing`
9. Send traffic from host: `iperf3 -c <vm_ip> -t 30 -l 64`
10. Read results: `cat /sys/kernel/debug/rx_timing`

---

## Assumptions & Unsupported Features

- **x86-64 only** — kprobe handlers use `regs->dx` / `regs->cx` (System V ABI). ARM64 not supported.
- **e1000 driver only** — policy hooks are inside `e1000_clean_rx_irq`. Other NICs are not patched.
- **QEMU/KVM recommended** — physical multi-socket NUMA machines also work but require BIOS NUMA enabled.
- **Only one policy module at a time** — simultaneous loading of both kprobe modules is untested.
- **Free-RAM-based dynamic policy** — code exists in `results/free_rambased_dynamic_allocation/` for reference; no loadable module is provided in this artifact.
- No known persistent crashes or deadlocks. Occasional `dmesg` warnings about kprobe re-registration if module is inserted without removing first (handled by `build_and_load.sh`).
