# current machine experiments

Extra runs on this machine. The final benchmark is still in `../README.md`.

Run this from `Capstone/10447`:
```bash
./experiments/run_current_machine.sh
```

It puts raw text files in `results/current_machine/`. The script does not need root.
`perf` is optional: if the kernel blocks a counter, it writes the error instead of
stopping everything.

## machine limits I am working around

The saved machine snapshot is a 2-core / 4-thread i3-4005U at 1.7 GHz, with roughly
4 GB RAM and 3 MB shared L3. This matters here:

- BookC starts with a large flat table (about 32 MB). That is fine on this machine,
  but I do it once before the hot loop. Growing it during a benchmark would make a
  random latency spike and would be hard to compare fairly with A/B.
- A and B allocate many small nodes as live orders grow. With only 3 MB L3, a few
  hundred thousand live orders do not fit in cache, so the pointer chasing is the
  thing I expected `perf` to show.
- I build the workload before timing. The first version of my perf command included
  workload generation in the whole-process counters; the final README calls that out.
  The stopwatch total in `bench.cpp` starts after workload creation.
- I do five final runs and use the median because this CPU has no turbo but it still
  moves around if the browser or package updates are doing work. Plugging in power and
  closing noisy programs is boring but changes these small numbers.

## failures worth keeping

`DEVLOG.md` has the real development failures: stale BBO after cancelling the best
level, a broken linear-probe chain after delete, and a table that eventually hung when
tombstones filled every slot. The old scratch headers were not preserved, so those
failure cases are documented rather than rebuilt. The current script has an expected
bad-CLI test just to show that a failure is reported cleanly and doesn't get confused
with a passing benchmark.

## next thing I tried: sparse-book scan

BookC normally looks O(1), but that is not the whole story. If its best level empties,
it walks the price array looking for the next non-empty level. The normal random
workload gets dense, so I added `next_steps.cpp` to make the bad case on purpose:

```bash
g++ -std=c++17 -O2 -o next_steps experiments/next_steps.cpp
./next_steps
```

It compares a dense book (next price is one tick away) with a sparse book (next price
is 4,999 ticks away). It also tries the bitmap idea from the next-steps list. The
bitmap part is only the price-search piece, not a new complete order book, so it is
not compared to the main A/B/C numbers. This is just the case that tells me whether
the cached-index trick has an ugly tail latency problem and whether the obvious fix is
worth carrying into the real code.
