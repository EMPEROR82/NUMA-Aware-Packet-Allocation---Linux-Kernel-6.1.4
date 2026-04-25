import os
import re
import glob
from collections import defaultdict

import matplotlib.pyplot as plt

BASE_DIR = "."      # folder containing 00/, 10/, 11/
OUT_DIR = "latency_plots"       # output root folder

# Matches: node_00_64_1.txt
FILE_RE = re.compile(r"node_(\d{2})_(\d+)_(\d)\.txt$")

# Matches data rows like:
# e1000_alloc_frag               2012547           174         19152
ROW_RE = re.compile(
    r"^(?P<stage>\S+)\s+(?P<count>\d+)\s+(?P<avg_ns>\d+(?:\.\d+)?|-)\s+(?P<max_ns>\d+(?:\.\d+)?|-)\s*$"
)

STAGES = [
    "e1000_alloc_frag",
    "napi_build_skb",
    "copybreak_alloc",
    "prefetch_gap",
    "napi_gro_receive",
    "__netif_receive_core",
    "ip_rcv_core",
    "tcp_v4_rcv",
    "tcp_queue_rcv",
    "copy_to_user",
    "e2e_softirq",
]

def parse_latency_file(path):
    """
    Returns dict: stage -> avg_ns (float or None)
    """
    stage_to_avg = {}

    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()

    # Only parse the table section
    in_table = False
    for line in lines:
        s = line.rstrip("\n")

        if s.startswith("stage") and "avg_ns" in s:
            in_table = True
            continue

        if not in_table:
            continue

        if not s.strip():
            continue

        if s.startswith("enabled:"):
            break

        m = ROW_RE.match(s)
        if not m:
            continue

        stage = m.group("stage")
        avg_ns = m.group("avg_ns")
        if avg_ns != "-":
            stage_to_avg[stage] = float(avg_ns)

    return stage_to_avg


# data[p][stage][node_cfg][packet_size] = avg_ns
data = defaultdict(lambda: defaultdict(lambda: defaultdict(dict)))

for path in glob.glob(os.path.join(BASE_DIR, "**", "node_*.txt"), recursive=True):
    fname = os.path.basename(path)
    m = FILE_RE.match(fname)
    if not m:
        continue

    node_cfg = m.group(1)   # 00 / 10 / 11
    pkt_size = int(m.group(2))
    p = int(m.group(3))      # 1 or 2

    stage_avgs = parse_latency_file(path)

    for stage, avg_ns in stage_avgs.items():
        data[p][stage][node_cfg][pkt_size] = avg_ns

# Create output folders and plots
for p in [1, 2]:
    p_dir = os.path.join(OUT_DIR, f"p_{p}")
    os.makedirs(p_dir, exist_ok=True)

    for stage in STAGES:
        plt.figure(figsize=(8, 5))

        for node_cfg in sorted(data[p][stage].keys()):
            pts = data[p][stage][node_cfg]
            if not pts:
                continue

            sizes = sorted(pts.keys())
            avgs = [pts[s] for s in sizes]
            plt.plot(sizes, avgs, marker="o", linewidth=2, label=f"node {node_cfg}")

        plt.xlabel("Packet Size")
        plt.ylabel("Average Latency (ns)")
        plt.title(f"{stage} | p = {p}")
        plt.grid(True, alpha=0.3)
        plt.legend()
        plt.tight_layout()

        out_file = os.path.join(p_dir, f"{stage}.png")
        plt.savefig(out_file, dpi=300)
        plt.close()

    # Combined plot for all stages
    fig, axes = plt.subplots(4, 3, figsize=(20, 15))
    axes = axes.flatten()

    for i, stage in enumerate(STAGES):
        ax = axes[i]
        for node_cfg in sorted(data[p][stage].keys()):
            pts = data[p][stage][node_cfg]
            if not pts:
                continue

            sizes = sorted(pts.keys())
            avgs = [pts[s] for s in sizes]
            ax.plot(sizes, avgs, marker="o", linewidth=2, label=f"node {node_cfg}")

        ax.set_xlabel("Packet Size")
        ax.set_ylabel("Average Latency (ns)")
        ax.set_title(f"{stage}")
        ax.grid(True, alpha=0.3)
        ax.legend()

    # Hide the last subplot if 11 stages
    if len(STAGES) < 12:
        axes[-1].set_visible(False)

    fig.suptitle(f"Latency Analysis for p = {p}", fontsize=16)
    plt.tight_layout()

    combined_out = os.path.join(OUT_DIR, f"combined_p_{p}.png")
    plt.savefig(combined_out, dpi=600, bbox_inches='tight')
    plt.close()

print(f"Done. Plots saved under: {OUT_DIR}/p_1 and {OUT_DIR}/p_2")