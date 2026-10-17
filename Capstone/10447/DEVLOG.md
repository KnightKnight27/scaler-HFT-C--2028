# dev log: building BookC (the "own design" one)

small honesty note before the story: this log was written from the scratch versions I
had while making BookC, but I only kept the final `book.h` in this folder. The old
`v1.h` ... `v6.h` snapshots are not here anymore, so `devlog/run_devlog.sh` cannot
rebuild those exact old versions right now. I left the failed cases in the log because
they are the actual bugs I hit, but I don't want to pretend the missing snapshots are
reproducible evidence.

For things that *are* reproducible on the machine being used for submission, see
`experiments/run_current_machine.sh`. It records CPU/RAM state, does small/medium/real
workload runs, runs verify, and records one expected command-line failure. That is the
one to rerun before making the PR.

how I actually got to the version in `book.h`. all timings below are from a quick
throwaway 100k-op harness on my dev machine (not the real 1M-op bench, not perf), just
to catch obviously-dumb vs obviously-better while iterating. real numbers are in README.md.

## v1 - vector of orders, linear scan for everything
Simplest thing that could work: push every order into a `vector`, scan it for dup-id
checks and for cancel. Price levels were already plain arrays.

**100k ops: 1923 ms.** Basically unusable (~19 µs/op avg). And it gets worse the longer
it runs, because cancelled orders are never removed from the vector, so every later
op scans more dead weight. Confirmed this isn't just slow, it's *getting* slower.

## v2 - swap the id lookup to unordered_map
Kept the arrays, kept a cached `best_bid`/`best_ask` index instead of scanning for BBO.
On purpose, left `cancel_order` *not* updating the cached best index, to see what breaks.

**100k ops: 9.6 ms** (200x faster than v1, just from fixing the O(N) lookup).

Then broke it on purpose:
```
b.add_order(1, Buy, 9999, 10);   // best bid
b.add_order(2, Buy, 9998, 5);
b.cancel_order(1);               // the only order at the best level
get_bbo() -> has_bid=1 tick=9999 qty=0     <- wrong, should be tick=9998 qty=5
```
Confirmed: cached best-index needs to be re-scanned when the level it points at empties,
not just left stale. This is exactly what Hint 2 in the assignment is hinting at.

## v3 - fix the stale best index
```cpp
if ((bq[i] -= o.qty) == 0 && i == best_bid)
    while (best_bid >= 0 && !bq[best_bid]) best_bid--;
```
Same targeted case now gives `tick=9998 qty=5`. Also checked the side-goes-fully-empty
case (`has_bid=0`). Timing: **100k ops: 7.4 ms**, no slower than before, because the
rescan only runs when the best level actually empties, not on every cancel.

## v4 - write my own hash table instead of unordered_map, attempt 1 (no tombstones)
`unordered_map` allocates a node per order, which is the thing I want to get rid of
(that's the "page faults from allocation" story the assignment wants me to find via
`perf`). Open addressing, linear probing. Delete just clears the slot.

Engineered a guaranteed collision (3 ids that all hash to the same slot), cancelled the
middle one, then looked up the last one:
```
cancel(id3) -> 0   (expected 1)
```
Broke it on purpose and it broke as expected: clearing a slot on delete ends the probe
chain for anything that landed after it, so the lookup gives up before reaching id3.
Classic open-addressing-without-tombstones bug.

## v5 - add tombstones
Delete marks the slot as a tombstone (not empty), so later probes keep going through it.
Insert reuses the first tombstone it sees instead of always walking to a truly empty slot.
Same collision case now gives `cancel(id3) -> 1`, correct.

Then stress tested it: 20,000 add+cancel cycles through a 4096-slot table, no rehashing.
**Hung.** `timeout 5` killed it at exit code 124, somewhere around iteration 4000-6000.
Reason: every slot eventually gets touched (used or tombstone) and never goes back to
"never touched" without a rehash, so `find()` on a missing key has no empty sentinel left
to stop at and loops forever.

## v6 - add rehashing
Rehash when the table is >70% full counting tombstones too (`(dirty+1)*10 > capacity*7`),
doubling capacity and dropping tombstones. Re-ran the exact stress test that hung v5, now
with 200,000 cycles (10x more than what killed v5): **no hang.** Collision-chain test
still passes. Timing back to **7.7 ms** for the 100k mixed workload. Also ran 50k mixed
ops through ASan + UBSan, clean.

## v7 - preallocate a big table up front
`init(1<<12)` was fine for the stress test, but for the real 1M-op benchmark a table
that starts at 4096 slots would rehash itself several times mid-run, which both costs
time and would be unfair to compare against A/B since those don't resize like that in
the middle of the benchmark. Preallocated `1<<20` slots up front instead, so rehash
essentially never fires during the real workload. Timing unchanged at this scale
(**7.7 ms**, table size doesn't matter until you're actually near the load factor).

This is what's in `book.h` now (names cleaned up, comments trimmed down).

## summary

| version | what changed | 100k ops | bug found |
|---|---|---:|---|
| v1 | vector, linear scan | 1923 ms | O(N) everywhere, gets worse over time |
| v2 | unordered_map for ids | 9.6 ms | stale best-bid/ask on cancel |
| v3 | fix stale best index | 7.4 ms | - |
| v4 | own hash table, no tombstones | - | delete breaks later probe chains |
| v5 | add tombstones | - | no rehash -> table fills -> infinite loop |
| v6 | add rehashing | 7.7 ms | - |
| v7 | preallocate 2^20 slots | 7.7 ms | - (this is the shipped version) |

v2->v3 and v6->v7 don't show a timing win in this small harness, the point of those
steps was correctness/robustness, not speed. The real speed win (v1->v2) came from
killing the O(N) id lookup, same lesson as Hint 1.

## what I would do differently if I started again

Keep every little versioned header next to this file. The code got cleaner when I
moved to one final `book.h`, but it also made the development story less reproducible.
For this submission the final correctness test and the current-machine experiment
script are the proof. The old v1-v6 notes are useful as a bug diary, not as source that
the reader can compile.
