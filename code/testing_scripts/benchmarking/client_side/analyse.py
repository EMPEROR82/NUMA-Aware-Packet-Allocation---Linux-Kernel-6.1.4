import os
import re
import glob
import pandas as pd
import matplotlib.pyplot as plt

# Folder that contains 00/, 10/, 11/ ...
BASE_DIR = "."

# Match files like: node_00_64_1.csv
FILE_RE = re.compile(r"node_(\d{2})_(\d+)_(\d)\.csv$")

# Collect data:
# data[p][node_cfg] = [(packet_size, throughput), ...]
data = {
    1: {},
    2: {}
}

for csv_path in glob.glob(os.path.join(BASE_DIR, "**", "node_*.csv"), recursive=True):
    fname = os.path.basename(csv_path)
    m = FILE_RE.match(fname)
    if not m:
        continue

    node_cfg, pkt_size, p = m.group(1), int(m.group(2)), int(m.group(3))
    if p not in data:
        continue

    df = pd.read_csv(csv_path)

    # Prefer AGGREGATE row
    agg = df[df["run_id"].astype(str) == "AGGREGATE"]
    if not agg.empty:
        val = str(agg.iloc[0]["throughput_mbps"])
        m2 = re.search(r"avg=([0-9.]+)", val)
        if m2:
            throughput = float(m2.group(1))
        else:
            throughput = pd.to_numeric(agg.iloc[0]["throughput_mbps"], errors="coerce")
    else:
        # Fallback: mean of numeric rows
        numeric = pd.to_numeric(df["throughput_mbps"], errors="coerce").dropna()
        throughput = numeric.mean() if not numeric.empty else None

    if throughput is None:
        continue

    data[p].setdefault(node_cfg, []).append((pkt_size, throughput))

# Plot one chart for p=1 and one for p=2
for p in [1, 2]:
    plt.figure(figsize=(9, 6))

    for node_cfg, points in sorted(data[p].items()):
        points = sorted(points, key=lambda x: x[0])
        sizes = [x for x, _ in points]
        thr = [y for _, y in points]
        plt.plot(sizes, thr, marker="o", linewidth=2, label=f"node {node_cfg}")

    plt.xlabel("Packet Size")
    plt.ylabel("Throughput (Mbps)")
    plt.title(f"Throughput Comparison for p = {p}")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()

    out_file = f"throughput_comparison_p{p}.png"
    plt.savefig(out_file, dpi=300)
    plt.show()

print("Done. Saved:")
print(" - throughput_comparison_p1.png")
print(" - throughput_comparison_p2.png")