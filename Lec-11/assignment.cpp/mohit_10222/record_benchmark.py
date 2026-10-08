#!/usr/bin/env python3
"""Record five one-second trials per lock, raw counters, environment and summary."""
import csv
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path
import platform
import statistics
import subprocess
import sys

root = Path(__file__).resolve().parent
output = root / (sys.argv[1] if len(sys.argv) > 1 else "results")
output.mkdir(parents=True, exist_ok=True)


def read_command(arguments):
    result = subprocess.run(arguments, cwd=root, text=True, capture_output=True, timeout=15)
    return result.stdout.strip() if result.returncode == 0 else None


build_command = ["make", "-B", "CXX=clang++", "build/spsc_bench"]
build = subprocess.run(build_command, cwd=root, text=True, capture_output=True, timeout=45)
if build.returncode != 0:
    sys.exit(build.stdout + build.stderr)

command = ["./build/spsc_bench", "1", "5", "1024", "both"]
started = datetime.now(timezone.utc).isoformat()
run = subprocess.run(command, cwd=root, text=True, capture_output=True, timeout=90)
ended = datetime.now(timezone.utc).isoformat()
(output / "benchmark.csv").write_text(run.stdout)
if run.returncode != 0:
    sys.exit("Benchmark failed; retained stdout. " + run.stderr)

rows = list(csv.DictReader(io.StringIO(run.stdout)))
if len(rows) != 10:
    sys.exit("Expected five measured trials per lock")
for row in rows:
    produced, consumed, completed, drained = (int(row[key]) for key in (
        "produced_total", "consumed_total", "completed_in_window", "drained_after_window"))
    if (produced != consumed or int(row["payload_errors"]) != 0 or
            completed <= 0 or completed + drained != consumed or
            row["object_bytes"] != "64" or float(row["window_seconds"]) != 1):
        sys.exit("Unexpected counters or verification failure: " + str(row))

summary = {}
for name in ("mutex", "spin"):
    counts = [int(row["completed_in_window"]) for row in rows if row["lock"] == name]
    if len(counts) != 5:
        sys.exit("Missing trials for " + name)
    median = statistics.median(counts)
    summary[name] = {
        "objects_per_second_trials": counts,
        "median_objects_per_second": median,
        "min_objects_per_second": min(counts),
        "max_objects_per_second": max(counts),
        "median_payload_MB_per_second": median * 64 / 1_000_000,
    }

environment = {
    "started_utc": started,
    "ended_utc": ended,
    "os": platform.platform(),
    "architecture": platform.machine(),
    "cpu": read_command(["sysctl", "-n", "machdep.cpu.brand_string"]),
    "logical_cpus": read_command(["sysctl", "-n", "hw.logicalcpu"]),
    "ram_bytes": read_command(["sysctl", "-n", "hw.memsize"]),
    "macos_version": read_command(["sw_vers", "-productVersion"]),
    "compiler": read_command(["clang++", "--version"]),
    "build_command": build_command,
    "build_output": build.stdout + build.stderr,
    "benchmark_command": command,
    "benchmark_binary_sha256": hashlib.sha256((root / "build/spsc_bench").read_bytes()).hexdigest(),
    "source_sha256": {
        name: hashlib.sha256((root / name).read_bytes()).hexdigest()
        for name in ("spsc_queue.cpp", "spsc_queue.hpp", "object64.hpp", "Makefile")
    },
    "warmup_seconds_per_variant": 0.1,
    "measurement": "Completed and timestamped pops within each common one-second window",
    "constraints": "Normal Mac scheduler; no CPU affinity or process isolation; background load uncontrolled",
}
for name, value in (("environment.json", environment), ("summary.json", summary)):
    (output / name).write_text(json.dumps(value, indent=2) + "\n")
print(json.dumps(summary, indent=2))
print("Raw measurements and metadata:", output)
