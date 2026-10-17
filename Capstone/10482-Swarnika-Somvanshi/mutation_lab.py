#!/usr/bin/env python3
"""mutation_lab.py - redo the three 'break it on purpose' experiments from the README
and save the REAL output of each broken build.

Run from inside Capstone/10482-Swarnika-Somvanshi/ :
    python3 mutation_lab.py
It works on a temp copy, so your source files are never touched.
It writes results/mutation_A.txt, results/mutation_B.txt, results/mutation_C.txt.
"""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCES = ["common.h", "implem_a.h", "implem_b.h", "implem_c.h", "workload.h", "test.cpp"]

MUTATIONS = {
    "A": ("implem_a.h",
          "if (it->second == 0) m.erase(it);",
          "if (it->second == 0) {}",
          "never erase a price level when its quantity reaches 0"),
    "B": ("implem_b.h",
          "bool before(int a, int b) const { return is_bid ? a < b : a > b; }",
          "bool before(int a, int b) const { return a < b; }",
          "before() always compares a < b, so ask levels are sorted the wrong way"),
    "C": ("implem_c.h",
          "while (best_bid_ >= 0 && bid_qty_[best_bid_] == 0) --best_bid_;",
          "while (best_bid_ > 0 && bid_qty_[best_bid_] == 0) --best_bid_;",
          "bid scan stops at index 0 instead of falling to -1 (empty book still reports a bid)"),
}

def build_and_run(workdir):
    r = subprocess.run(["g++", "-O2", "-std=c++17", "test.cpp", "-o", "t"],
                       cwd=workdir, capture_output=True, text=True)
    if r.returncode != 0:
        return -1, "BUILD ERROR:\n" + r.stderr
    p = subprocess.run(["./t"], cwd=workdir, capture_output=True, text=True, timeout=300)
    return p.returncode, p.stdout

def main():
    os.makedirs(os.path.join(HERE, "results"), exist_ok=True)

    # baseline first: the unbroken code must pass, otherwise the experiment means nothing
    d = tempfile.mkdtemp(prefix="mut_")
    for f in SOURCES: shutil.copy(os.path.join(HERE, f), d)
    rc, out = build_and_run(d); shutil.rmtree(d, ignore_errors=True)
    print(f"baseline (unbroken code): exit={rc}")
    if rc != 0:
        print(out); sys.exit("Fix the baseline first.")

    for name, (fname, old, new, why) in MUTATIONS.items():
        d = tempfile.mkdtemp(prefix="mut_")
        for f in SOURCES: shutil.copy(os.path.join(HERE, f), d)
        path = os.path.join(d, fname)
        text = open(path).read()
        if old not in text:
            sys.exit(f"mutation {name}: pattern not found in {fname} (did the file change?)")
        open(path, "w").write(text.replace(old, new, 1))
        rc, out = build_and_run(d)
        shutil.rmtree(d, ignore_errors=True)

        header = (f"# Mutation {name}: {why}\n"
                  f"# File: {fname}\n"
                  f"# Original: {old}\n"
                  f"# Broken:   {new}\n"
                  f"# (output below is from ./test run on the BROKEN copy)\n\n")
        with open(os.path.join(HERE, "results", f"mutation_{name}.txt"), "w") as fh:
            fh.write(header + out + f"\n# exit code: {rc}\n")
        verdict = "CAUGHT" if rc != 0 else "NOT CAUGHT"
        print(f"mutation {name}: {verdict} (exit {rc}) -> results/mutation_{name}.txt")

if __name__ == "__main__":
    main()
