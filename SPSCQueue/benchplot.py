#!/usr/bin/env python3

import subprocess
import re
import matplotlib.pyplot as plt

EXECUTABLE = "./build/Desktop_Debug/SPSCQueue"
MILLISECONDS = 1000
MAX_THREADS = 10


def run_benchmark(threads):
    command = [EXECUTABLE, str(MILLISECONDS), str(threads)]

    print(f"\nRunning with {threads} threads...")

    result = subprocess.run(
        command,
        capture_output=True,
        text=True,
        check=True,
    )

    print(result.stdout)

    match = re.search(r"Total ops:\s*(\d+)", result.stdout)

    if not match:
        raise RuntimeError(
            f"Could not find 'Total ops' in output:\n{result.stdout}"
        )

    return int(match.group(1))


def main():
    thread_counts = list(range(1, MAX_THREADS + 1))
    total_ops = []

    for threads in thread_counts:
        ops = run_benchmark(threads)
        total_ops.append(ops)

    print("\nBenchmark Summary")
    print("-" * 35)

    for threads, ops in zip(thread_counts, total_ops):
        print(
            f"{threads:2d} threads: "
            f"{ops:>12,} total ops "
            f"({ops / (MILLISECONDS / 1000):,.0f} ops/sec)"
        )

    plt.figure(figsize=(12, 6))

    bars = plt.bar(thread_counts, total_ops, width=0.7)

    plt.title("SPSCQueue Benchmark", fontsize=16)
    plt.xlabel("Number of Concurrent Jobs (Threads)")
    plt.ylabel("Total Operations per Second")
    plt.xticks(thread_counts)
    plt.grid(axis="y", linestyle="--", alpha=0.4)

    plt.bar_label(
        bars,
        labels=[f"{ops / (MILLISECONDS / 1000):,.0f}" for ops in total_ops],
        padding=3,
        rotation=45,
        fontsize=8,
    )

    plt.tight_layout()
    plt.savefig("benchmark.png", dpi=200)
    plt.show()


if __name__ == "__main__":
    main()
