# Capstone: Low-Latency Order Book

Garvit Dadheech
Roll no: 23BCS10203
Email: garvit.23bcs10203@ms.sst.scaler.com

Report with the results table, perf output and analysis: [REPORT.md](REPORT.md)

## Files

```
Makefile              make / make verify / make run / make perf / make apple-pmu
src/common.hpp        Side, Level, BBO and the price <-> tick helpers
src/book_map.hpp      Implementation A : std::map + std::list + std::unordered_map
src/book_heap.hpp     Implementation B : unordered_maps + priority_queue (lazy deletion)
src/book_array.hpp    Implementation C : fixed array + bitmap + open addressing hash table
src/workload.hpp      random workload generator (mt19937 seed 42)
src/runner.hpp        runs a workload through a book and times each op
src/bench.cpp         the benchmark / verify program
tools/apple_pmu.cpp   reads hardware counters on my Mac (M2), see Profiling below
scripts/run_perf.sh   perf stat + perf record for A, B, C on Linux
scripts/run_apple_pmu.sh
results/              all the raw outputs used in REPORT.md
```

## Build

```
make                              # builds build/bench with g++ -O2 -std=c++17
g++ -O2 -std=c++17 src/bench.cpp -o bench   # same thing as one command
```

Only the standard library is used. Compiles clean with -Wall -Wextra -Wpedantic
on Apple clang 17 and g++ 13.

## Correctness test

```
make verify            # runs ./build/bench --verify
```

This generates the same 1,000,000 op workload used by the benchmark and pushes
every op through A, B and C together. After every single op it compares the BBO
of B and C against A. On every depth snapshot (every 100 ops) it compares
get_bids/get_asks at depth 10 and also at full depth, so every price level and
quantity in the book is checked. Return values of cancel_order are compared too.
Before that it runs the edge cases from the spec on each implementation: empty
book, cancelling an id that does not exist or was already cancelled, several
orders on the same price adding up, get_bids(10) with only 3 levels, and the
best price moving when the best level is emptied.

Output (results/verify.txt):

```
Running correctness test...

Workload: 1000000 ops, seed 42
Checked : 1010000 BBO snapshots, 199496 cancel results, 10000 depth snapshots (top-10 and full book), plus edge cases

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

## Benchmark

```
./build/bench --impl A
./build/bench --impl B
./build/bench --impl C
./build/bench --impl none    # empty book, measures just the loop + timers
make run                     # A, B, C one after the other
```

Flags: `--ops N` (default 1,000,000), `--seed S` (default 42), `--reps R`
(default 5, median is printed), `--no-timers` (don't time individual ops, only
the whole loop; I use this under perf so the profile is only order book code).

The workload is 70% add, 20% cancel, 10% get_bbo, and after every 100 ops one
get_bids(10) + get_asks(10). Side is 50/50, qty is 1-100, price is a random
tick in the side's band, cancels always pick an order that is live (if a
cancel is drawn while the book is empty it becomes an add). The whole op list
is generated before the timed loop so the RNG never runs inside a measurement.
Every op type is timed on its own with steady_clock. At the end the final BBO
and a checksum of every value the book returned is printed so the compiler
can't drop any of the calls.

One thing I ran into: `std::uniform_int_distribution` gives different numbers
on libstdc++ and libc++ for the same mt19937, so the Linux and Mac runs were
getting different workloads. I replaced it with a plain modulo on the raw
mt19937 output (which is specified by the standard), so now the checksum is
the same on every machine.

## Profiling

### perf (Linux)

```
make perf        # runs scripts/run_perf.sh
```

For each of A, B, C (and the `none` baseline) the script runs

```
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
./build-linux/bench --impl A --reps 1 --no-timers

perf record -F 20000 -g ./build-linux-profile/bench --impl A --reps 3 --no-timers
perf report --stdio --no-children
```

For perf record I build with `-g -fno-omit-frame-pointer -DOB_PROFILE`.
OB_PROFILE just marks the five public methods noinline so perf report can show
add_order / cancel_order by name instead of everything being inlined into the
loop. perf stat uses the normal -O2 build.

My laptop is an M2 MacBook so there is no Linux perf on it. I ran the script in
an Ubuntu 24.04 docker container (g++ 13.3, perf 6.8):

```
docker build -t ob-perf - <<'EOF2'
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y g++ make linux-tools-generic linux-tools-common libc6-dbg
RUN ln -sf $(ls -d /usr/lib/linux-tools/*/ | head -1)perf /usr/local/bin/perf
EOF2
docker run --rm --privileged -v "$PWD":/work -w /work ob-perf \
    bash -c "sysctl -w kernel.perf_event_paranoid=-1; ./scripts/run_perf.sh"
```

The VM does not get the PMU, so cycles, instructions, branches, branch-misses,
cache-misses, L1-dcache-load-misses and dTLB-load-misses come out as
`<not supported>`. task-clock and page-faults work, and perf record samples on
cpu-clock. The script works as is on a real Linux box, where all counters work.

### Hardware counters on the Mac

To still get cycles / IPC / branch misses / L1 misses from my own machine I
wrote tools/apple_pmu.cpp. It uses the kperf framework (the thing Instruments
uses for its CPU Counters template) to read the M2 counters around the same
loop that bench runs. It has to run as root:

```
sudo ./scripts/run_apple_pmu.sh     # writes results/apple_pmu_{A,B,C}.txt
```

Apple's PMU does not have a last level cache miss event, so cache-misses can't
be measured on this machine at all.

## Interface

I kept the API exactly as given (double price, long qty, the five methods).
Inside, every implementation converts the price to an integer tick
(`llround(price * 100)`) when an order comes in and converts back when
returning a Level or BBO, so no comparisons or indexing use doubles.

Small additions: each class has a static `name()` that the benchmark prints,
and ArrayBook (C) takes an optional constructor argument for the expected
number of live orders to pre-size its table (default 2^19). HeapBook (B) calls
reserve on its hash maps in the constructor.

The classes are MapBook, HeapBook and ArrayBook. The benchmark is a template
over the book type, so there is no virtual call in the measured path.

## The three designs

### A: MapBook (src/book_map.hpp)

The obvious version with standard containers.

- Price levels: `std::map<int, PriceLevel>` per side. Bids use `std::greater`
  so begin() is the best bid, asks use `std::less`.
- Orders: each PriceLevel has a `std::list<Order>` in arrival order and the
  total qty of the level.
- Best bid/ask: begin() of each map.
- Cancel: `std::unordered_map<id, Handle>`; Handle has the side, tick and the
  list iterator, so after a map::find on the tick the node is unlinked in O(1).

| op | complexity (L = levels on a side) |
|---|---|
| add_order | O(log L): tree walk + list push_back + hash insert |
| cancel_order | O(log L): hash find + tree walk + list erase (+ map erase if level is empty) |
| get_bbo | O(1) |
| get_bids(d) | O(d), walks the tree in order |
| get_asks(d) | O(d) |

### B: HeapBook (src/book_heap.hpp)

No ordered container except two heaps, and the heaps are only used for
"what is the best price".

- Orders: `std::unordered_map<id, Order{side, tick, qty}>`.
- Price levels: `std::unordered_map<int, long>` per side (tick -> total qty).
  A level is erased when its qty hits 0.
- Best bid/ask: `std::priority_queue<int>` (max heap for bids, min heap for
  asks). A tick is pushed when its level is created. Nothing is removed when a
  level goes away; instead, when the top of the heap is a tick that is no
  longer in the level map, it gets popped (lazy deletion). The same tick can be
  in the heap twice if a level empties and is created again, which is fine at
  the top and is deduplicated in get_bids.
- Cancel: hash lookup of the id, then update the level map. The heap is not
  touched.

| op | complexity (H = heap size including stale entries) |
|---|---|
| add_order | O(1) avg, plus O(log H) push when a level is new |
| cancel_order | O(1) avg |
| get_bbo | O(1) amortised, each stale entry is popped once |
| get_bids(d) | O(H + d log H): copies the heap and pops d live levels |
| get_asks(d) | same |

### C: ArrayBook (src/book_array.hpp), my own design

The price range is fixed at 10,000 ticks, so there is no need for any ordered
container at all, and nothing has to be allocated per order.

- Price levels: `long qty_[10001]` indexed by tick - 5000. 0..4999 are bids,
  5001..10000 are asks, 5000 is the mid and is never used.
- Best bid/ask: I keep the index of the best level on each side. An add just
  compares. When a cancel empties the best level I need the next non-empty
  level, and for that there is a bitmap with one bit per level plus a summary
  word with one bit per 64-bit word. With clz/ctz the next set bit is found in
  O(1) instead of scanning empty levels.
- Orders: a flat open addressing hash table of 16 byte slots {id, tick, qty},
  linear probing, and backward shift deletion so there are no tombstones. The
  hash is the splitmix64 finaliser. Side is not stored because the tick tells
  you the side. The table is sized up front and doubles at 70% load.
- Cancel: probe the table, read tick and qty from the slot, remove the slot.

| op | complexity |
|---|---|
| add_order | O(1): probe + array add + set bit + compare |
| cancel_order | O(1): probe + array subtract (+ O(1) bitmap lookup if the best level emptied) |
| get_bbo | O(1), reads the cached indices |
| get_bids(d) | O(d) bitmap steps |
| get_asks(d) | O(d) |

### How they differ

| | A | B | C |
|---|---|---|---|
| orders live in | list nodes inside each level | unordered_map nodes | one flat array |
| levels live in | red-black tree | unordered_map | fixed array by tick |
| best price | tree begin() | heap top + lazy pops | cached index + bitmap |
| find order to cancel | hash -> list iterator, then tree find | hash -> level hash | linear probe |
| allocations per add | 2 (+1 when a level is new) | 1 (+1 when a level is new) | 0 |
