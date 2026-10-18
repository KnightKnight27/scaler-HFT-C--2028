#!/usr/bin/env python3
"""Runs ./build/SPSCQueue for 1 second per lock and thread placement, 5 times
each, then prints a summary and saves a bar chart of the median to benchmark.png.

Thread placements (CPU numbers are for an 8-core / 16-thread Ryzen where CPUs
2 and 3 are the two hyper-threads of one core; check yours with `lscpu -e`):
  - not pinned:      the OS decides where t1 and t2 run
  - same core:       t1 and t2 on the two hyper-threads of one physical core
  - different cores: t1 and t2 on two different physical cores
"""

import re
import statistics
import subprocess

import matplotlib.pyplot as plt

EXECUTABLE = "./build/SPSCQueue"
MILLISECONDS = 1000
REPEATS = 5
LOCKS = ["spinlock", "mutex"]
PLACEMENTS = [
    ("not pinned", []),
    ("same core (CPU 2 + 3)", ["2", "3"]),
    ("different cores (CPU 2 + 4)", ["2", "4"]),
]


def run_once(lock, cpus):
    out = subprocess.run(
        [EXECUTABLE, str(MILLISECONDS), lock, *cpus],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    match = re.search(r"objects pushed\+popped per second:\s*(\d+)", out)
    if not match or "FIFO check: PASS" not in out:
        raise RuntimeError(f"Unexpected output:\n{out}")
    return int(match.group(1))


def main():
    medians = {}  # (placement, lock) -> median objects/second
    for placement, cpus in PLACEMENTS:
        for lock in LOCKS:
            runs = [run_once(lock, cpus) for _ in range(REPEATS)]
            medians[(placement, lock)] = statistics.median(runs)
            print(f"{placement:28s} {lock:8s} runs: {', '.join(f'{r:,}' for r in runs)}")

    print("\n| Thread placement | spinlock (objects/s) | mutex (objects/s) |")
    print("|---|---:|---:|")
    for placement, _ in PLACEMENTS:
        print(f"| {placement} | {medians[(placement, 'spinlock')]:,.0f} | {medians[(placement, 'mutex')]:,.0f} |")

    fig, ax = plt.subplots(figsize=(12, 6))
    width = 0.38
    xs = range(len(PLACEMENTS))
    for i, lock in enumerate(LOCKS):
        values = [medians[(p, lock)] for p, _ in PLACEMENTS]
        bars = ax.bar([x + (i - 0.5) * width for x in xs], values, width, label=lock)
        ax.bar_label(bars, labels=[f"{v / 1e6:.1f}M" for v in values], padding=3, fontsize=9)

    ax.set_title(f"SPSCQueue: 64-byte objects pushed + popped per second (median of {REPEATS} x 1 s)", fontsize=14)
    ax.set_xlabel("Where the producer (t1) and consumer (t2) threads run")
    ax.set_ylabel("Objects per second")
    ax.set_xticks(list(xs))
    ax.set_xticklabels([p for p, _ in PLACEMENTS])
    ax.yaxis.set_major_formatter(plt.FuncFormatter(lambda v, _: f"{v / 1e6:.0f}M"))
    ax.grid(axis="y", linestyle="--", alpha=0.4)
    ax.legend()

    fig.tight_layout()
    fig.savefig("benchmark.png", dpi=200)


if __name__ == "__main__":
    main()
