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
| Run | Pushed | Popped |
|-----|--------|--------|
| 1 | 7,982,552 | 7,982,552 |
| 2 | 8,082,066 | 8,082,066 |
| 3 | 8,168,248 | 8,168,248 |
| 4 | 7,546,601 | 7,546,601 |
| 5 | 7,845,584 | 7,845,584 |
| **Average** | **~7.9 million / sec** | **~7.9 million / sec** |

Every message pushed was popped (no losses).

Measured with g++ -O2 on a 4-core ARM64 (aarch64) Linux machine.

Sample output:
```
objects pushed in 1 second: 7982552
objects popped in 1 second: 7982552
```
