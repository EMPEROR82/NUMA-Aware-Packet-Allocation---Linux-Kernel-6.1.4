import os
import re
import glob
import argparse
import pandas as pd
import matplotlib.pyplot as plt


def parse_aggregate(csv_path):
    """Return average throughput (Mbps) from the AGGREGATE row, or mean of all rows."""
    try:
        df = pd.read_csv(csv_path)
    except Exception as e:
        print(f"  [!] Cannot read {csv_path}: {e}")
        return None

    agg = df[df["run_id"].astype(str) == "AGGREGATE"]
    if not agg.empty:
        val = str(agg.iloc[0]["throughput_mbps"])
        m = re.search(r"avg=([0-9.]+)", val)
        if m:
            return float(m.group(1))
        numeric = pd.to_numeric(agg.iloc[0]["throughput_mbps"], errors="coerce")
        return float(numeric) if not pd.isna(numeric) else None

    # Fallback: mean of all numeric rows
    numeric = pd.to_numeric(df["throughput_mbps"], errors="coerce").dropna()
    return float(numeric.mean()) if not numeric.empty else None


def main():
    parser = argparse.ArgumentParser(
        description="Plot throughput results from CSV files produced by script.py"
    )
    parser.add_argument("--dir", default=".", help="Folder containing .csv files (default: .)")
    parser.add_argument("--out", default="throughput", help="Output image prefix (default: throughput)")
    args = parser.parse_args()

    csv_files = sorted(glob.glob(os.path.join(args.dir, "**", "*.csv"), recursive=True))
    if not csv_files:
        print(f"[!] No .csv files found under '{args.dir}'")
        return

    labels = []
    throughputs = []

    for path in csv_files:
        label = os.path.splitext(os.path.basename(path))[0]
        val   = parse_aggregate(path)
        if val is not None:
            labels.append(label)
            throughputs.append(val)
            print(f"  {label}: {val:.2f} Mbps")
        else:
            print(f"  [!] Skipped (no data): {label}")

    if not labels:
        print("[!] No valid data found.")
        return

    # ── Bar chart: one bar per file ───────────────────────────────────────────
    fig, ax = plt.subplots(figsize=(max(8, len(labels) * 1.2), 5))
    bars = ax.bar(labels, throughputs, color="steelblue", edgecolor="black", width=0.6)
    ax.bar_label(bars, fmt="%.1f", padding=3, fontsize=8)
    ax.set_xlabel("Result File")
    ax.set_ylabel("Throughput (Mbps)")
    ax.set_title("Throughput per Run")
    ax.set_xticks(range(len(labels)))
    ax.set_xticklabels(labels, rotation=35, ha="right", fontsize=8)
    ax.grid(axis="y", alpha=0.3)
    plt.tight_layout()
    bar_out = f"{args.out}_bar.png"
    plt.savefig(bar_out, dpi=300)
    plt.close()
    print(f"\n[+] Saved: {bar_out}")

if __name__ == "__main__":
    main()