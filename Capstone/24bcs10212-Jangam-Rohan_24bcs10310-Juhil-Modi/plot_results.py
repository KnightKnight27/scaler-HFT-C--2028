#reads the text files in results/ and saves bar charts into results/graphs/
#run from this folder: python plot_results.py

import os
import re

import matplotlib.pyplot as plt

IMPLS = ["A", "B", "C"]
NAMES = ["A: map", "B: sorted vector", "C: price array"]
COLORS = ["#4C72B0", "#DD8452", "#55A868"]
OUT_DIR = "results/graphs"


def read(path):
    with open(path) as f:
        return f.read()


def find_number(text, pattern):
    return float(re.search(pattern, text).group(1).replace(",", ""))


def bar_chart(title, ylabel, values, filename):
    plt.figure(figsize=(6, 4))
    bars = plt.bar(NAMES, values, color=COLORS)
    plt.bar_label(bars, fmt="{:,.1f}")
    plt.title(title)
    plt.ylabel(ylabel)
    plt.tight_layout()
    plt.savefig(os.path.join(OUT_DIR, filename))
    plt.close()


def grouped_chart(title, ylabel, groups, filename):
    plt.figure(figsize=(7, 4))
    ops = ["add", "cancel", "bbo"]
    for i in range(3):
        x = [j + (i - 1) * 0.25 for j in range(len(ops))]
        bars = plt.bar(x, groups[i], width=0.25, color=COLORS[i], label=NAMES[i])
        plt.bar_label(bars, fmt="{:.0f}", fontsize=8)
    plt.xticks(range(len(ops)), ops)
    plt.ylabel(ylabel)
    plt.title(title)
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(OUT_DIR, filename))
    plt.close()


os.makedirs(OUT_DIR, exist_ok=True)

total, avg, tail = [], [], []
for impl in IMPLS:
    text = read(f"results/bench_{impl}.txt")
    total.append(find_number(text, r"Total time\s*:\s*([\d.]+)"))
    avg.append([find_number(text, rf"ns / {op}\s*:\s*([\d.]+)") for op in ["add", "cancel", "bbo"]])
    tail.append([find_number(text, rf"ns / {op}\s*:.*p99 ([\d.]+)") for op in ["add", "cancel", "bbo"]])

page_faults = []
for impl in IMPLS:
    text = read(f"results/perf_stat_{impl}.txt")
    page_faults.append(find_number(text, r"([\d,]+)\s+page-faults"))

#cachegrind numbers, shown in millions
instructions, l1_misses, mispredicts = [], [], []
for impl in IMPLS:
    text = read(f"results/cachegrind_{impl}.txt")
    instructions.append(find_number(text, r"I refs:\s+([\d,]+)") / 1e6)
    l1_misses.append(find_number(text, r"D1  misses:\s+[\d,]+\s+\(\s*([\d,]+) rd") / 1e6)
    mispredicts.append(find_number(text, r"Mispredicts:\s+([\d,]+)") / 1e6)

grouped_chart("Average latency per operation (lower is better)", "ns per op, median of 5 runs", avg, "latency_per_op.png")
grouped_chart("p99 latency per operation (lower is better)", "ns, 99% of ops are faster", tail, "p99_latency.png")
bar_chart("Total time for 1M ops (median of 5)", "ms", total, "total_time.png")
bar_chart("Page faults (perf stat, 5 runs per process)", "page faults", page_faults, "page_faults.png")
bar_chart("Instructions (cachegrind, 1 run)", "millions", instructions, "instructions.png")
bar_chart("L1 data read misses (cachegrind, simulated)", "millions", l1_misses, "l1_misses.png")
bar_chart("Branch mispredicts (cachegrind, simulated)", "millions", mispredicts, "branch_mispredicts.png")

print("saved graphs to", OUT_DIR)
