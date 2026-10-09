# SPSC Queue with Mutex Locks

- email: akshat.24bcs10059@sst.scaler.com

- roll_no: 10059

---

## 1. Design
- Queue: `MutexSPSCQueue<T>` in `spsc_queue_mutex.cpp` (`std::mutex` + `lock_guard`)

- Bench: `spsc_queue.cpp` (`t1` producer + `t2` consumer, `t1.join()` + `t2.join()`)
- Object: `struct alignas(64) Msg64 { char data[64]; }` (`static_assert` 64B)
- Memory pool: ring preallocated once (`vector::resize(cap)`), no per-op `new`

## 2. Build & Run
```bash
clang++ -std=c++20 -O2 -Wall -Wextra spsc_queue.cpp -o main.out
./main.out
```

## 3. Results
| Metric | Value |
|--------|-------|
| cap | 65536 |
| window | 1s (+1s drain grace) |
| pushed | 6688926 |
| popped | 6688926 |
| push/sec | 3344417 |
| pop/sec | 3344417 |

Sample output:
```
cap=65536 window=1s total_wall=2.00003s
pushed=6688926 popped=6688926
push/sec~3344417 pop/sec~3344417
note: mutex SPSC, 64B objects, t1+t2 joined
```



