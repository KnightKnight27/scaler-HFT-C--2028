#!/usr/bin/env python3
"""Exercise argument errors and the actual timed producer/consumer entry point."""
import csv
import io
import math
import subprocess
import sys

binary = sys.argv[1]
checks = 0


def invoke(arguments):
    return subprocess.run([binary, *arguments], text=True, capture_output=True, timeout=45)


def check(condition, description):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(description)


help_result = invoke(["--help"])
check(help_result.returncode == 0 and "Usage:" in help_result.stdout, "help")

for arguments in [
    ["0"], ["-1"], ["nan"], ["inf"], ["61"], ["garbage"], ["1junk"],
    ["0.01", "0"], ["0.01", "-1"], ["0.01", "1", "0"],
    ["0.01", "1", "7", "unknown"], ["0.01", "1001"],
    ["0.01", "1", "1048577"], ["0.01", "1", "7", "both", "extra"],
]:
    result = invoke(arguments)
    check(result.returncode != 0 and "Error:" in result.stderr,
          f"reject malformed/unsupported arguments {arguments}")

for mode in ("mutex", "spin", "both"):
    result = invoke(["0.03", "2", "7", mode])
    check(result.returncode == 0, f"benchmark {mode}: {result.stderr}")
    rows = list(csv.DictReader(io.StringIO(result.stdout)))
    check(len(rows) == (4 if mode == "both" else 2), "trial count")
    check({row["lock"] for row in rows} == ({"mutex", "spin"} if mode == "both" else {mode}),
          "requested lock variants")
    for row in rows:
        produced = int(row["produced_total"])
        consumed = int(row["consumed_total"])
        completed = int(row["completed_in_window"])
        drained = int(row["drained_after_window"])
        window = float(row["window_seconds"])
        check(row["capacity"] == "7" and row["object_bytes"] == "64", "queue/object size")
        check(produced == consumed and produced > 0, "full drain and successful transfers")
        check(0 < completed <= consumed and completed + drained == consumed,
              "window count excludes drain and is not double counted")
        check(int(row["payload_errors"]) == 0, "all eight words and sequence verified")
        check(float(row["elapsed_including_drain_seconds"]) >= window, "timing window reached")
        check(math.isclose(float(row["objects_per_second"]), completed / window, rel_tol=1e-8),
              "reported rate matches actual count and requested time")

print(f"PASS {checks} CLI/benchmark checks")
