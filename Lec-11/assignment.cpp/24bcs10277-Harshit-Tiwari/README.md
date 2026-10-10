email: "harshit.24bcs10277@sst.scaler.com"

roll_no: "24bcs10277"

# Lec-11: SPSC queue with locks

A queue with one producer thread (`t1`) and one consumer thread (`t2`), protected by a lock. It runs for 1 second and counts how many 64-byte objects get popped. Code is in [`spsc_queue.cpp`](./spsc_queue.cpp).

## Result: 64-byte objects per second

| Lock | Objects / second (5 runs) | Median |
| :--- | :--- | ---: |
| Spinlock (`atomic_flag` while loop) | 4.6 M to 7.0 M | 5.9 M (about 0.37 GB/s) |
| `std::mutex` | 3.3 M to 4.0 M | 3.6 M (about 0.23 GB/s) |

So with locks, roughly **4 to 6 million 64-byte objects per second** go through the queue on my machine. The spinlock was ahead of the mutex in all 5 runs.

## How it works

- `Obj` is a 64-byte struct (`static_assert` checks the size). It holds a sequence number so the consumer can check the order.
- The queue is a ring buffer of 1024 slots in a `std::vector`, allocated once before the run. That is the memory pool, so push and pop never allocate.
- One lock protects both `push` and `pop`. The same queue class is used with `SpinLock` (a `while` loop on `atomic_flag::test_and_set`) and with `std::mutex`.
- `t1` keeps pushing, `t2` keeps popping, `main` sleeps for 1 second, sets a stop flag, then `t1.join()` and `t2.join()`.
- The program prints `pushed`, `popped` and `out_of_order`. `out_of_order` is 0 in every run. `popped` can be a bit lower than `pushed` because some objects are still in the queue when the 1 second ends.
- Checked with `-Wall -Wextra -Wpedantic` (no warnings) and with ThreadSanitizer (no reports).

## Build and run

```bash
g++ -std=c++17 -O2 -pthread spsc_queue.cpp -o spsc_queue
./spsc_queue
```

## Machine

Intel Core i3-1115G4 (2 cores, 4 threads), Windows 11 with Linux (WSL2) running g++ 16.2.0 in Docker, `-O2`.

## Raw output (not pinned)

```text
spinlock   pushed=7040283 popped=7039493 (7.04 M/s) out_of_order=0
mutex      pushed=4045290 popped=4044751 (4.04 M/s) out_of_order=0
spinlock   pushed=4562162 popped=4561139 (4.56 M/s) out_of_order=0
mutex      pushed=3335818 popped=3335818 (3.34 M/s) out_of_order=0
spinlock   pushed=6667060 popped=6666037 (6.67 M/s) out_of_order=0
mutex      pushed=4011563 popped=4011562 (4.01 M/s) out_of_order=0
spinlock   pushed=5855306 popped=5854284 (5.85 M/s) out_of_order=0
mutex      pushed=3642922 popped=3641899 (3.64 M/s) out_of_order=0
spinlock   pushed=5794263 popped=5793486 (5.79 M/s) out_of_order=0
mutex      pushed=3615745 popped=3615745 (3.62 M/s) out_of_order=0
```

## Notes

- The numbers move around from run to run because this is a laptop running Linux inside a virtual machine. When I pinned both threads to two CPUs with `taskset -c 0,2`, the spinlock median was 3.2 M/s and the mutex median 3.6 M/s, with the mutex ranging from 0.9 M/s to 3.9 M/s. So in that setup the two locks were not clearly different, and I would not claim the spinlock always wins.
- The work per lock is tiny (copy 64 bytes), so most of the time goes into the threads fighting for the lock and moving its cache line between cores, not into the copy.
