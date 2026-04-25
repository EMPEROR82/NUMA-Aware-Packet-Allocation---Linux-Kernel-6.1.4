import subprocess
import json
import csv
import argparse
import statistics
import time

# ── Defaults ──────────────────────────────────────────────────────────────────
DEFAULT_RUNS     = 1
DEFAULT_SIZES    = [64, 128, 256, 512, 1024]   # bytes
DEFAULT_PARALLEL = [1, 2]
DEFAULT_DURATION = 120                          # seconds
SERVER           = "192.168.100.2"
# ──────────────────────────────────────────────────────────────────────────────


def run_iperf(parallel, pkt_size, duration):
    cmd = [
        "iperf3",
        "-c", SERVER,
        "-t", str(duration),
        "-P", str(parallel),
        "-l", str(pkt_size),
        "-J"
    ]
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=duration + 10
        )
        data = json.loads(result.stdout)
        bps  = data["end"]["sum_sent"]["bits_per_second"]
        return bps / 1e6   # Mbps
    except Exception as e:
        print(f"  [!] iperf3 error: {e}")
        return None


def main():
    parser = argparse.ArgumentParser(
        description="Run iperf3 benchmarks against the VM and save results to CSV."
    )
    parser.add_argument("--output",   type=str, required=True,
                        help="Output CSV file name (e.g. baseline_64_p1.csv)")
    parser.add_argument("--runs",     type=int, default=DEFAULT_RUNS,
                        help=f"Number of runs per config (default: {DEFAULT_RUNS})")
    parser.add_argument("--size",     type=int, nargs="+", default=DEFAULT_SIZES,
                        help=f"Packet size(s) in bytes (default: {DEFAULT_SIZES})")
    parser.add_argument("--parallel", type=int, nargs="+", default=DEFAULT_PARALLEL,
                        help=f"Number of parallel streams (default: {DEFAULT_PARALLEL})")
    parser.add_argument("--duration", type=int, default=DEFAULT_DURATION,
                        help=f"iperf3 duration per run in seconds (default: {DEFAULT_DURATION})")
    args = parser.parse_args()

    sizes    = args.size
    parallel = args.parallel
    runs     = args.runs
    duration = args.duration

    print(f"Server      : {SERVER}")
    print(f"Packet sizes: {sizes} B")
    print(f"Streams     : {parallel}")
    print(f"Runs        : {runs}")
    print(f"Duration    : {duration} s/run")
    print(f"Output      : {args.output}")
    print()

    with open(args.output, "w", newline="") as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow(["parallel_streams", "packet_size", "run_id", "throughput_mbps"])

        for p in parallel:
            for pkt in sizes:
                print(f"=== Streams={p}, Packet={pkt}B ===")
                results = []

                for r in range(1, runs + 1):
                    print(f"  Run {r}/{runs} ...", end=" ", flush=True)
                    throughput = run_iperf(p, pkt, duration)

                    if throughput is not None:
                        print(f"{throughput:.2f} Mbps")
                        writer.writerow([p, pkt, r, f"{throughput:.4f}"])
                        results.append(throughput)
                    else:
                        print("FAILED")
                        writer.writerow([p, pkt, r, "ERROR"])

                    csvfile.flush()
                    if r < runs:
                        time.sleep(2)

                if results:
                    avg = statistics.mean(results)
                    mn  = min(results)
                    mx  = max(results)
                    std = statistics.stdev(results) if len(results) > 1 else 0.0
                    print(f"  → Avg: {avg:.2f}  Min: {mn:.2f}  Max: {mx:.2f}  Std: {std:.2f} Mbps")
                    writer.writerow([
                        p, pkt, "AGGREGATE",
                        f"avg={avg:.2f};min={mn:.2f};max={mx:.2f};std={std:.2f}"
                    ])
                    csvfile.flush()
                print()


if __name__ == "__main__":
    main()
