import subprocess
import json
import csv
import argparse
import statistics
import time
import random

DURATION = 10
SERVER   = "192.168.100.2"
SEED     = 42          # fixed seed — change here to get a different reproducible sequence

def run_iperf(pkt_size):
    cmd = [
        "iperf3",
        "-c", SERVER,
        "-t", str(DURATION),
        "-P", "1",
        "-l", str(pkt_size),
        "-J"
    ]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=DURATION + 5)
        data   = json.loads(result.stdout)
        bps    = data["end"]["sum_sent"]["bits_per_second"]
        return bps / 1e6          # → Mbps
    except Exception as e:
        print(f"  [error] {e}")
        return None


def main():
    parser = argparse.ArgumentParser(
        description="Run iperf3 with randomly sampled packet sizes in a given range."
    )
    parser.add_argument(
        "--min-size", type=int, required=True, metavar="MIN",
        help="Lower bound of the packet-size range (bytes, inclusive)"
    )
    parser.add_argument(
        "--max-size", type=int, required=True, metavar="MAX",
        help="Upper bound of the packet-size range (bytes, inclusive)"
    )
    parser.add_argument(
        "--runs", type=int, default=10, metavar="N",
        help="How many random packet sizes to sample (default: 10)"
    )
    parser.add_argument(
        "-o", "--output", type=str, required=True, metavar="FILE",
        help="Path to the output CSV file"
    )
    args = parser.parse_args()

    if args.min_size < 1 or args.max_size < args.min_size:
        parser.error("Must satisfy: 1 <= min-size <= max-size")

    # Seed the RNG once so the packet-size sequence is reproducible
    rng = random.Random(SEED)
    packet_sizes = [rng.randint(args.min_size, args.max_size) for _ in range(args.runs)]

    print(f"Seed         : {SEED}")
    print(f"Packet range : [{args.min_size}, {args.max_size}] bytes")
    print(f"Sampled sizes: {packet_sizes}")
    print(f"Output file  : {args.output}\n")

    with open(args.output, "w", newline="") as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow([
            "run_id", "packet_size_bytes", "seed",
            "pkt_range_min", "pkt_range_max", "throughput_mbps"
        ])

        results = []

        for run_id, pkt_size in enumerate(packet_sizes, start=1):
            print(f"=== Run {run_id}/{args.runs}  |  packet size = {pkt_size} B ===")
            throughput = run_iperf(pkt_size)

            if throughput is not None:
                print(f"  → Throughput: {throughput:.2f} Mbps")
                writer.writerow([
                    run_id, pkt_size, SEED,
                    args.min_size, args.max_size,
                    f"{throughput:.4f}"
                ])
                results.append((pkt_size, throughput))
            else:
                print("  → Run failed, skipping.")
                writer.writerow([
                    run_id, pkt_size, SEED,
                    args.min_size, args.max_size,
                    "FAILED"
                ])

            csvfile.flush()
            time.sleep(1)

        # ---- aggregate summary ----
        if results:
            tputs = [t for _, t in results]
            avg   = statistics.mean(tputs)
            mn    = min(tputs)
            mx    = max(tputs)
            std   = statistics.stdev(tputs) if len(tputs) > 1 else 0.0

            summary = f"avg={avg:.2f};min={mn:.2f};max={mx:.2f};std={std:.2f}"
            writer.writerow([
                "AGGREGATE", "N/A", SEED,
                args.min_size, args.max_size,
                summary
            ])
            print(f"\n--- Aggregate ({len(tputs)} successful runs) ---")
            print(f"  Avg : {avg:.2f} Mbps")
            print(f"  Min : {mn:.2f} Mbps")
            print(f"  Max : {mx:.2f} Mbps")
            print(f"  Std : {std:.2f} Mbps")
        else:
            print("\n[!] No successful runs — nothing to aggregate.")


if __name__ == "__main__":
    main()
