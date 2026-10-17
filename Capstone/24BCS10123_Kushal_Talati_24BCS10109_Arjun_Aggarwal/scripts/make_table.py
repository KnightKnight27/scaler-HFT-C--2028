#!/usr/bin/env python3
"""builds results/table.md from the raw files. stdlib only, run from the submission folder.
  results/linux/bench_X.json   timed runs on linux (docker)  -> ns/op rows
  perf/stat_X.txt              perf stat -r 5 on linux       -> IPC, branch miss %, cache rows, page faults
  results/macos/bench_X.json   timed runs on the mac         -> second small table
first table is exactly the one from section 10 of the assignment. n/a = counter was <not supported>."""
import json, os, re, statistics

IMPLS = ["A", "B", "C"]

def runs(path):
    if not os.path.exists(path): return None
    with open(path) as f:
        return [json.loads(l) for l in f if l.strip()]

def med(rs, key):
    return statistics.median(r[key] for r in rs)

def parse_stat(path):
    # perf stat -r lines look like:  "  1,234,567      cycles   #  1.23  insn per cycle  ( +- 0.5% )"
    out = {}
    if not os.path.exists(path): return out
    for line in open(path):
        m = re.match(r"\s*([\d,\.]+|<not supported>|<not counted>)\s+(\S+)", line)
        if not m: continue
        val, ev = m.group(1), m.group(2).split(":")[0]
        out[ev] = None if val.startswith("<") else float(val.replace(",", ""))
        d = re.search(r"#\s+([\d\.]+)\s+insn per cycle", line)
        if d: out["ipc"] = float(d.group(1))
        d = re.search(r"#\s+([\d\.]+)%\s+of all branches", line)
        if d: out["branch_miss_pct"] = float(d.group(1))
    return out

def fmt(v, nd=1):
    if v is None: return "n/a"
    if abs(v) >= 1000: return f"{v:,.0f}"
    return f"{v:.{nd}f}"

def table(rows):
    return "| Metric | Implementation A | Implementation B | Implementation C |\n|---|---:|---:|---:|\n" + \
           "\n".join(f"| {label} | " + " | ".join(vals) + " |" for label, vals in rows)

def main():
    linux = {x: runs(f"results/linux/bench_{x}.json") for x in IMPLS}
    mac = {x: runs(f"results/macos/bench_{x}.json") for x in IMPLS}
    stat = {x: parse_stat(f"perf/stat_{x}.txt") for x in IMPLS}
    timed = lambda src, key: [fmt(med(src[x], key)) if src[x] else "n/a" for x in IMPLS]

    first = next(v for v in linux.values() if v)
    print(f"Linux (ubuntu docker container), median of {len(first)} runs, {first[0]['ops']:,} ops, seed {first[0]['seed']}. "
          f"perf rows from `perf stat -r 5`, n/a = `<not supported>` in the VM.\n")
    print(table([
        ("Total time (ms)",       timed(linux, "total_ms")),
        ("ns / add",              timed(linux, "ns_add")),
        ("ns / cancel",           timed(linux, "ns_cancel")),
        ("ns / BBO",              timed(linux, "ns_bbo")),
        ("IPC",                   [fmt(stat[x].get("ipc"), 2) for x in IMPLS]),
        ("Branch misses (%)",     [fmt(stat[x].get("branch_miss_pct"), 2) for x in IMPLS]),
        ("L1-dcache-load-misses", [fmt(stat[x].get("L1-dcache-load-misses")) for x in IMPLS]),
        ("Cache misses",          [fmt(stat[x].get("cache-misses")) for x in IMPLS]),
        ("Page faults",           [fmt(stat[x].get("page-faults"), 0) for x in IMPLS]),
    ]))
    if any(mac.values()):
        print(f"\nSame benchmark natively on the mac (apple clang), median of 5. no perf on macOS.\n")
        print(table([
            ("Total time (ms)", timed(mac, "total_ms")),
            ("ns / add",        timed(mac, "ns_add")),
            ("ns / cancel",     timed(mac, "ns_cancel")),
            ("ns / BBO",        timed(mac, "ns_bbo")),
        ]))

if __name__ == "__main__":
    main()
