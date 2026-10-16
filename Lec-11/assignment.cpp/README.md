# SPSC Queue with Mutex Locks

- email: akshat.24bcs10059@sst.scaler.com

- roll_no: 10059

---

## 1. Design
- Queue: `MutexSPSCQueue<T>` in `spsc_queue_mutex.cpp` (`std::mutex` + `lock_guard`)
- Bench: `spsc_queue.cpp`
  - `producer()` runs on thread `t1`: pushes messages for 1 second
  - `consumer()` runs on thread `t2`: pops messages until the producer is done, then empties the queue
  - `t1.join()` + `t2.join()`, then prints the counts
- Object: `struct Msg64 { char data[64]; }` (`static_assert` 64B)
- Memory pool: ring buffer preallocated once (capacity 65536), no per-op `new`

## 2. Build & Run
```bash
clang++ -std=c++20 -O2 -Wall -Wextra spsc_queue.cpp -o main.out
./main.out
```
(`g++ -std=c++20 -O2 -Wall -Wextra -pthread spsc_queue.cpp -o main.out` also works)

## 3. Results (5 runs, 1 second each)
Machine: Apple M3 (macOS), Apple clang 21.0.0, `-O2`

| Run | Pushed | Popped |
|-----|--------|--------|
| 1 | 18,715,430 | 18,715,430 |
| 2 | 18,878,093 | 18,878,093 |
| 3 | 19,054,767 | 19,054,767 |
| 4 | 19,011,569 | 19,011,569 |
| 5 | 18,987,798 | 18,987,798 |
| **Average** | **~18.9 million / sec** | **~18.9 million / sec** |

Every message pushed was popped (no losses).

Sample output:
```
objects pushed in 1 second: 18715430
objects popped in 1 second: 18715430
```
