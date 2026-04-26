# Benchmarking Guide — NUMA-Aware Packet Allocation

**Setup:** VM = iperf3 server (receiver) | Remote Host = iperf3 client (sender)

---

## 1. VM Network Setup

On the VM, set the IP address on the interface connected to the host:

```bash
# Replace eth0 with the actual interface name (ip link to check)
sudo ip addr add 192.168.100.2/24 dev eth0
sudo ip link set eth0 up

# Verify
ip addr show eth0
ping 192.168.100.1   # ping the host side to confirm connectivity
```

> If using QEMU TAP networking, the host-side TAP IP is typically `192.168.100.1`.

---

## 2. VM Side — Before Each Test Run

### 2a. Start iperf3 Server

Open a terminal on the VM and keep this running throughout:

```bash
iperf3 -s
# Listens on port 5201 by default
```

### 2b. Reset Latency Counters

Before each experiment, reset the rx_timing counters so old data doesn't bleed in:

```bash
echo r | sudo tee /sys/kernel/debug/rx_timing
```

### 2c. Enable Latency Collection

```bash
echo 1 | sudo tee /sys/kernel/debug/rx_timing
```

---

## 3. Remote Host (Client) Side — Run the Test

`script.py` is in `code/testing_scripts/benchmarking/client_side/`.

### Usage

```
python3 script.py --output <name.csv> [options]
```

| Flag | Default | Description |
|------|---------|-------------|
| `--output` | *required* | Output CSV filename |
| `--size` | `64 128 256 512 1024` | Packet size(s) in bytes (space-separated) |
| `--parallel` | `1 2` | Number of parallel iperf3 streams (space-separated) |
| `--runs` | `1` | Number of repeat runs per config |
| `--duration` | `120` | Duration of each iperf3 run in seconds |

### Examples

```bash
# Full sweep with defaults (all sizes, p=1 and p=2, 1 run, 120s each)
python3 script.py --output baseline.csv

# Single packet size, single stream, 3 runs of 60s
python3 script.py --output policy1_64_p1.csv --size 64 --parallel 1 --runs 3 --duration 60

# Multiple sizes, single stream
python3 script.py --output policy1_p1.csv --size 64 128 256 512 1024 --parallel 1 --duration 120

# Quick smoke test
python3 script.py --output smoke.csv --size 256 --parallel 1 --runs 1 --duration 10
```

The script prints progress in real time and writes results to the CSV as it goes (flushed after each run).

**CSV output format:**

```
parallel_streams,packet_size,run_id,throughput_mbps
1,64,1,482.3100
1,64,AGGREGATE,avg=482.31;min=482.31;max=482.31;std=0.00
```

---

## 4. VM Side — Collect Latency Data After Each Run

Once the client-side test finishes (or after a specific configuration), snapshot the latency counters:

```bash
# Save to a named file — use a descriptive name matching your CSV
sudo cat /sys/kernel/debug/rx_timing > baseline_64_p1.txt

# Reset for the next experiment
echo r | sudo tee /sys/kernel/debug/rx_timing
```

> **Naming tip:** Use the same base name for your `.csv` (client) and `.txt` (server) files.  
> Example: `policy1_64_p1.csv` on the client → `policy1_64_p1.txt` on the server.

**Latency file format:**

```
RX stack timing (ns)
==========================================================
stage                     count        avg_ns        max_ns
-----                     -----        ------        ------
e1000_alloc_frag        2012547           174         19152
napi_build_skb          2012547           312         25600
...
enabled: 1
```

---

## 5. Full Experiment Workflow (Per Policy)

```
VM                                        HOST
────────────────────────────────────────────────────────────
[Load policy module]
sudo insmod static_allocation_policy.ko
./set_node.sh 1 0 1 0

[Reset counters]
echo r | sudo tee /sys/kernel/debug/rx_timing
echo 1 | sudo tee /sys/kernel/debug/rx_timing

[Start iperf3 server]
iperf3 -s
                                          [Run client]
                                          python3 script.py \
                                            --output node0_64_p1.csv \
                                            --size 64 --parallel 1 \
                                            --duration 120

[Collect latency]
sudo cat /sys/kernel/debug/rx_timing > node0_64_p1.txt
echo r | sudo tee /sys/kernel/debug/rx_timing

                                          [Repeat for next size/policy]
```

---

## 6. Plotting Results

There are **two analysis scripts on each side**. Choose based on how you named your output files:

| Script | When to use | Input naming |
|--------|-------------|--------------|
| `analyse_results_gen.py` | Any experiment — flexible, no naming rules | Any `.csv` / `.txt` filename |
| `analyse.py` | Structured multi-experiment comparison | Strict: `node_<cfg>_<size>_<P>.csv/.txt` |

---

### Option A — `analyse_results_gen.py` (Flexible, Recommended for Quick Plots)

Reads **all** CSV or TXT files in a folder, uses the **filename (without extension)** as the X-axis label, and produces bar charts. No naming convention required.

#### Client side (throughput)

```bash
cd code/testing_scripts/benchmarking/client_side/

python3 analyse_results_gen.py [--dir <folder>] [--out <prefix>]
```

| Flag | Default | Description |
|------|---------|-------------|
| `--dir` | `.` | Folder to search for `.csv` files (recursive) |
| `--out` | `throughput` | Output image filename prefix |

**Output:**
- `throughput_bar.png` — one bar per CSV file, X-axis = filename

**Example:**
```bash
# All CSVs in current directory → throughput_bar.png
python3 analyse_results_gen.py

# CSVs in a specific folder
python3 analyse_results_gen.py --dir ~/results/policy1 --out policy1_throughput
```

---

#### Server side (latency)

```bash
cd code/testing_scripts/benchmarking/server_side/

python3 analyse_results_gen.py [--dir <folder>] [--out <output_dir>]
```

| Flag | Default | Description |
|------|---------|-------------|
| `--dir` | `.` | Folder to search for `.txt` latency files (recursive) |
| `--out` | `latency_plots` | Output directory for all plots |

**Output (inside `latency_plots/`):**
- One `<stage>.png` per RX stage — bar chart, X-axis = filename
- `combined_latency.png` — all 11 stages in a 4×3 subplot grid

**Example:**
```bash
# All TXTs in current directory → latency_plots/
python3 analyse_results_gen.py

# TXTs in a specific folder
python3 analyse_results_gen.py --dir ~/results/latency --out ~/results/latency_plots
```

**Dependencies:** `pip3 install matplotlib pandas`

---

### Option B — `analyse.py` (Structured Multi-Experiment Comparison)

Designed for comparing **multiple node configurations across packet sizes and parallel stream counts**. It expects files named with a strict pattern and plots **latency or throughput vs. packet size**, with one line per node configuration.

#### Required filename format

```
node_<cfg>_<pktsize>_<P>.csv      ← client side
node_<cfg>_<pktsize>_<P>.txt      ← server side
```

| Field | Meaning | Examples |
|-------|---------|---------|
| `<cfg>` | 2-digit node config code | `00` = baseline, `10` = pages forced, `11` = pages+SKBs forced |
| `<pktsize>` | Packet size in bytes | `64`, `128`, `256`, `512`, `1024` |
| `<P>` | Number of parallel streams | `1`, `2` |

**Example filenames:**
```
node_00_64_1.csv    ← baseline, 64B packets, 1 stream
node_10_64_1.csv    ← pages→node0, 64B, 1 stream
node_11_64_1.csv    ← pages+SKBs→node0, 64B, 1 stream
node_00_1024_2.csv  ← baseline, 1024B packets, 2 streams
```

Place all files for a given experiment **anywhere under one root directory** — flat or in subfolders, it doesn't matter. The script searches recursively (`glob **`) for all matching filenames under `BASE_DIR` (default: `.`).

> **No special folder structure required.** The old comment in the script mentions `00/`, `10/`, `11/` subdirectories — ignore it. A flat folder of files works fine:
> ```
> my_results/
>   node_00_64_1.csv
>   node_10_64_1.csv
>   node_11_64_1.csv
>   node_00_128_1.csv
>   ...
> ```

#### Client side (throughput)

```bash
cd code/testing_scripts/benchmarking/client_side/

# Place node_*.csv files in the current directory, then:
python3 analyse.py
```

**Output (in current directory):**
- `throughput_comparison_p1.png` — throughput vs. packet size for 1 stream
- `throughput_comparison_p2.png` — throughput vs. packet size for 2 streams

Each chart has **packet size on the X-axis** and plots **one line per node config code** (e.g. `node 00`, `node 10`, `node 11`).

#### Server side (latency)

```bash
cd code/testing_scripts/benchmarking/server_side/

# Place node_*.txt files in the current directory, then:
python3 analyse.py
```

**Output (inside `latency_plots/`):**
- `p_1/<stage>.png` — latency vs. packet size for 1 stream, one line per node config
- `p_2/<stage>.png` — latency vs. packet size for 2 streams
- `combined_p_1.png` / `combined_p_2.png` — all 11 stages in one grid per stream count

**Dependencies:** `pip3 install matplotlib pandas`

---

### Which Script Should I Use?

```
Quick comparison of a few named runs?
  → analyse_results_gen.py   (drop files anywhere, run, done)

Full structured sweep over sizes, streams, and node configs?
  → analyse.py               (name files as node_XX_SIZE_P, then run)
```


## 7. Recommended Experiment Matrix

| Experiment | Module | `set_node.sh` args | Output file prefix |
|---|---|---|---|
| Baseline (no policy) | none | — | `baseline` |
| Static → Node 0 (pages + SKBs) | `static_allocation_policy` | `1 0 1 0` | `static_node0` |
| Static → Node 1 (pages + SKBs) | `static_allocation_policy` | `1 1 1 1` | `static_node1` |
| Static → Node 0 (pages only) | `static_allocation_policy` | `1 0 0 0` | `static_node0_pagesonly` |
| Proportion dynamic | `force_node_proportion` | — | `proportion` |

Run each with the packet sizes and parallel stream counts you care about, e.g.:

```bash
# Example for static node 0, packet size 64, 1 stream
python3 script.py --output static_node0_64_p1.csv \
    --size 64 --parallel 1 --runs 3 --duration 120
```

---

## 8. Disabling Latency Collection

When not benchmarking, disable the timer overhead:

```bash
echo 0 | sudo tee /sys/kernel/debug/rx_timing
```

---

## 9. Randomised Packet-Size Testing (`randomised_script.py`)

### What It Does

`randomised_script.py` is an alternative to `script.py` for situations where you want to test **throughput across a continuous range of packet sizes** rather than a fixed list. Instead of sweeping pre-defined sizes, it:

1. Takes a `[min-size, max-size]` range from the user
2. Randomly samples `--runs` packet sizes from that range using a **fixed seed** (`SEED = 42` by default)
3. Runs one iperf3 test per sampled size (single stream, 10 s each by default)
4. Writes per-run results and an AGGREGATE summary to a CSV file

The fixed seed means the **same sequence of packet sizes is reproduced every time** you run with the same arguments — useful for comparing policies under identical load conditions.

> **Key difference from `script.py`:** No parallel-streams or duration flags — it always uses 1 stream and 10-second runs. The randomness is over packet sizes, not configuration parameters. To change the duration or seed, edit the constants at the top of the file (`DURATION`, `SEED`).

---

### Usage

```
python3 randomised_script.py --min-size MIN --max-size MAX [--runs N] -o OUTPUT
```

**Constraint:** `1 <= min-size <= max-size`

---

### Examples

```bash
# 10 random sizes between 64 and 1024 bytes → 10 runs of 10s each
python3 randomised_script.py --min-size 64 --max-size 1024 -o rand_baseline.csv

# 20 random sizes in the small-packet range (stress test the numabreak threshold)
python3 randomised_script.py --min-size 64 --max-size 512 --runs 20 -o rand_small.csv

# Wide range sweep for the proportion-based policy
python3 randomised_script.py --min-size 64 --max-size 8192 --runs 30 -o rand_proportion.csv
```

The script prints the full sampled sequence before starting, so you know exactly what will run:

```
Seed         : 42
Packet range : [64, 1024] bytes
Sampled sizes: [721, 389, 56, ...]
Output file  : rand_baseline.csv
```

---

### CSV Output Format

```
run_id,packet_size_bytes,seed,pkt_range_min,pkt_range_max,throughput_mbps
1,721,42,64,1024,487.3210
2,389,42,64,1024,512.8800
...
AGGREGATE,N/A,42,64,1024,avg=499.10;min=421.30;max=541.20;std=28.40
```

The `seed`, `pkt_range_min`, and `pkt_range_max` columns are written on every row so the CSV is self-documenting — you can reconstruct the exact test conditions from the file alone.

---

### Reproducibility

The RNG is seeded once at startup with `SEED = 42`. To run a **different** random sequence, change the `SEED` constant at the top of the file:

```python
SEED = 42   # change this for a different but still reproducible sequence
```

Two runs with the same seed and same `--min-size`/`--max-size`/`--runs` arguments will always produce **identical packet-size sequences**, making it valid to compare results across policies.

---

### Plotting Randomised Results

Use `analyse_results_gen.py` (client side) since filenames are freely chosen:

```bash
python3 analyse_results_gen.py --dir . --out rand_comparison
# → rand_comparison_bar.png  (one bar per CSV, X-axis = filename)
```

For a scatter-style view (throughput vs. actual packet size), you can adapt `analyse.py` or plot manually from the CSV using the `packet_size_bytes` column.
