# Lock-based SPSC queue benchmark

## email: adam.24bcs10017@sst.scaler.com
## roll_no: 24bcs10017

The Lec-11 assignment implements a bounded, single-producer/single-consumer ring
queue for 64-byte `Item`s. Queue slots come from one contiguous, 64-byte-aligned
`ItemPool` allocation made when the queue is constructed, so `push` and `pop`
do not allocate. The queue can use either `std::mutex` or an
`std::atomic_flag` spinlock; both expose non-blocking `push`/`pop` operations
that return `false` when full or empty. The head, tail, and lock state are
cache-line aligned.

### Build and run

```sh
cmake -B build
cmake --build build
```

Single-configuration generators default to Release. With the Visual Studio
multi-configuration generator, explicitly build and run the Release executable:

```powershell
cmake --build build --config Release
.\build\Release\spsc_benchmark.exe
```

The benchmark runs one producer and one consumer for a one-second
`steady_clock` interval per lock variant. The consumer drains any remaining
items before joining and validates the sequence number in every item. The
program exits with an error if sequence order or pushed/popped counts do not
match. MB/s uses decimal megabytes (ops/sec times 64 bytes divided by 1,000,000).

### Measured results

| Variant | Items pushed | Items popped | Ops/sec | MB/s |
|---|---:|---:|---:|---:|
| mutex | 14,068,048 | 14,068,048 | 13,890,030.59 | 888.96 |
| spinlock | 17,050,446 | 17,050,446 | 16,927,405.77 | 1,083.35 |

Measurement environment: Intel Core Ultra 5 226V (8 cores, 8 logical
processors), Windows 11 Home Single Language (build 26300), MSVC 19.51.36260.0
from Visual Studio 18, x64 Release configuration, C++17, and compiler flags
`/O2 /Ob2 /DNDEBUG`.

The spinlock was faster in this run, likely because the critical sections are
short and yielding while spinning avoided the additional mutex parking and
wakeup overhead. This result is workload- and machine-dependent: a spinlock
consumes CPU while waiting and can perform worse under preemption or heavier
contention. Both lock-based variants serialize every push and pop and transfer
ownership of the lock's cache line between cores. A lock-free SPSC queue using
separate atomic head/tail indices and per-slot synchronization would likely
reduce that contention and improve throughput.
