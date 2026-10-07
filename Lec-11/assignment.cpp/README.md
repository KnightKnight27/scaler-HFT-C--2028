---
email: "prabal.24bcs10031@sst.scaler.com"
roll_no: "24BCS10031"
---

# SPSC Queue Benchmark

Implementation and evaluation of a bounded SPSC queue comparing a spinlock vs `std::mutex` for 64-byte objects over a 1.0-second window.

## Benchmark Results (1.0s Window)

<img width="3024" height="864" alt="image" src="https://github.com/user-attachments/assets/0b4d45b8-1941-42f4-a03d-5699c5c129d9" />

| Primitive | Pushed | Popped | Throughput |
| :--- | :--- | :--- | :--- |
| **SpinLock** (while loop) | 7,971,252 | 7,971,251 | ~7.93M ops/s |
| **`std::mutex`** | 19,684,070 | 19,620,465 | **~19.52M ops/s** |

## Implementation Notes

- **Payload:** 64-byte message struct (`sizeof(Message) == 64`) with an 8-byte id and 56-byte payload.
- **Memory Pool:** Pre-allocated fixed-size ring buffer of 65,536 elements to avoid heap allocations in the hot path.
- **Observations:** `std::mutex` achieved higher throughput than the spinlock (~2.46x). With two threads running on separate cores, the spinlock burns CPU cycles spinning on the atomic flag while waiting, whereas `std::mutex` yields when contended so the thread holding the lock can finish its push or pop.

## Build & Run

```bash
clang++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -pthread Lec-11/assignment.cpp/spsc_queue.cpp -o spsc_queue
./spsc_queue
```
