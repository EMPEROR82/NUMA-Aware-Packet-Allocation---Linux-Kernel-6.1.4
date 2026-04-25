import subprocess
import json
import csv
import argparse
import statistics
import time

#PARALLEL_STREAMS = [1, 2]
#PACKET_SIZES = [64, 128, 256, 512, 1024]  # bytes
DURATION = 120
SERVER = "192.168.100.2"   # since you tunneled locally to remote VM

def run_iperf(parallel, pkt_size):
    cmd = [
        "iperf3",
        "-c", SERVER,
        "-t", str(DURATION),
        "-P", str(parallel),
        "-l", str(pkt_size),
        "-J"
    ]

    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=DURATION+5)
        data = json.loads(result.stdout)

        # Extract sender throughput (bits/sec → Mbps)
        bps = data["end"]["sum_sent"]["bits_per_second"]
        mbps = bps / 1e6

        return mbps

    except Exception as e:
        print(f"Error: {e}")
        return None


def main():
    parser = argparse.ArgumentParser()
    #parser.add_argument("--runs", type=int, required=True)
    parser.add_argument("-o", "--output", type=str, required=True)
    parser.add_argument("-s", "--size", type=int, required=True)
    parser.add_argument("-p", "--parallel", type=int, required=True)
    args = parser.parse_args()

    with open(args.output, "w", newline="") as csvfile:
        writer = csv.writer(csvfile)

        # Header
        writer.writerow([
            "parallel_streams", "packet_size",
            "run_id", "throughput_mbps"
        ])

        for __ in range(0, 1):
            p = args.parallel
            for _ in range(0, 1):
                pkt = args.size
                print(f"\n=== Config: Streams={p}, Packet={pkt}B ===")

                results = []

                for r in range(1, 2):
                    print(f"Running...")

                    throughput = run_iperf(p, pkt)

                    if throughput is not None:
                        print(f"  → Throughput: {throughput:.2f} Mbps")

                        writer.writerow([p, pkt, r, throughput])
                        results.append(throughput)
                    else:
                        print("  → Failed run")

                    csvfile.flush()
                    time.sleep(1)  # small gap between runs

                # Aggregate stats
                if results:
                    avg = statistics.mean(results)
                    mn = min(results)
                    mx = max(results)
                    std = statistics.stdev(results) if len(results) > 1 else 0

                    print(f"--- Aggregate ---")
                    print(f"Avg: {avg:.2f} Mbps | Min: {mn:.2f} | Max: {mx:.2f} | Std: {std:.2f}")

                    writer.writerow([
                        p, pkt, "AGGREGATE",
                        f"avg={avg:.2f};min={mn:.2f};max={mx:.2f};std={std:.2f}"
                    ])
                    csvfile.flush()


if __name__ == "__main__":
    main()
