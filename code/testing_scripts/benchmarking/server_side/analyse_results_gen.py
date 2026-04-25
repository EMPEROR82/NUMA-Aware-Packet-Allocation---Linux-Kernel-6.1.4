import os
import re
import glob
import argparse
from collections import defaultdict

import matplotlib.pyplot as plt


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

ROW_RE = re.compile(
    r"^(?P<stage>\S+)\s+(?P<count>\d+)\s+(?P<avg_ns>\d+(?:\.\d+)?|-)\s+(?P<max_ns>\d+(?:\.\d+)?|-)\s*$"
)


def parse_latency_file(path):
    """Returns dict: stage -> avg_ns (float). Skips stages where avg_ns is '-'."""
    result = {}
    in_table = False

    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            s = line.rstrip("\n")

            if s.startswith("stage") and "avg_ns" in s:
                in_table = True
                continue
            if not in_table:
                continue
            if not s.strip() or s.startswith("---") or s.startswith("RX stack"):
                continue
            if s.startswith("enabled:"):
                break

            m = ROW_RE.match(s)
            if not m:
                continue

            avg_ns = m.group("avg_ns")
            if avg_ns != "-":
                result[m.group("stage")] = float(avg_ns)

    return result


def main():
    parser = argparse.ArgumentParser(
        description="Plot per-stage latency from rx_timing .txt files."
    )
    parser.add_argument("--dir", default=".", help="Folder containing .txt latency files (default: .)")
    parser.add_argument("--out", default="latency_plots", help="Output directory for plots (default: latency_plots)")
    args = parser.parse_args()

    txt_files = sorted(glob.glob(os.path.join(args.dir, "**", "*.txt"), recursive=True))
    if not txt_files:
        print(f"[!] No .txt files found under '{args.dir}'")
        return

    os.makedirs(args.out, exist_ok=True)

    # data[stage][label] = avg_ns
    data = defaultdict(dict)
    labels = []

    for path in txt_files:
        label = os.path.splitext(os.path.basename(path))[0]
        parsed = parse_latency_file(path)
        if not parsed:
            print(f"  [!] No data parsed from: {path}")
            continue
        labels.append(label)
        for stage, avg_ns in parsed.items():
            data[stage][label] = avg_ns
        print(f"  Loaded: {label}  ({len(parsed)} stages)")

    if not labels:
        print("[!] No valid latency data found.")
        return

    # ── Per-stage bar charts ──────────────────────────────────────────────────
    for stage in STAGES:
        stage_data = data.get(stage, {})
        if not stage_data:
            continue

        ordered_labels = [l for l in labels if l in stage_data]
        values = [stage_data[l] for l in ordered_labels]

        fig, ax = plt.subplots(figsize=(max(8, len(ordered_labels) * 1.2), 5))
        bars = ax.bar(ordered_labels, values, color="coral", edgecolor="black", width=0.6)
        ax.bar_label(bars, fmt="%.0f ns", padding=3, fontsize=8)
        ax.set_xlabel("Result File")
        ax.set_ylabel("Average Latency (ns)")
        ax.set_title(f"Stage: {stage}")
        ax.set_xticks(range(len(ordered_labels)))
        ax.set_xticklabels(ordered_labels, rotation=35, ha="right", fontsize=8)
        ax.grid(axis="y", alpha=0.3)
        plt.tight_layout()

        out_file = os.path.join(args.out, f"{stage}.png")
        plt.savefig(out_file, dpi=300)
        plt.close()
        print(f"  [+] {out_file}")

    # ── Combined subplot grid ─────────────────────────────────────────────────
    n_stages = len(STAGES)
    ncols = 3
    nrows = (n_stages + ncols - 1) // ncols  # ceil division

    fig, axes = plt.subplots(nrows, ncols, figsize=(ncols * 7, nrows * 4))
    axes = axes.flatten()

    for i, stage in enumerate(STAGES):
        ax = axes[i]
        stage_data = data.get(stage, {})
        ordered_labels = [l for l in labels if l in stage_data]
        values = [stage_data[l] for l in ordered_labels]

        if ordered_labels:
            ax.bar(ordered_labels, values, color="coral", edgecolor="black", width=0.6)
        ax.set_title(stage, fontsize=9)
        ax.set_ylabel("avg ns", fontsize=8)
        ax.set_xticks(range(len(ordered_labels)))
        ax.set_xticklabels(ordered_labels, rotation=40, ha="right", fontsize=7)
        ax.grid(axis="y", alpha=0.3)

    # Hide unused subplots
    for j in range(n_stages, len(axes)):
        axes[j].set_visible(False)

    fig.suptitle("Per-Stage Latency Summary", fontsize=14)
    plt.tight_layout()
    combined_out = os.path.join(args.out, "combined_latency.png")
    plt.savefig(combined_out, dpi=200, bbox_inches="tight")
    plt.close()
    print(f"\n[+] Combined plot: {combined_out}")
    print(f"[+] All plots saved under: {args.out}/")


if __name__ == "__main__":
    main()