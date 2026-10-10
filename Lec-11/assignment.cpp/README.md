# SPSC Queue with Mutex Locks

- **Email:** tanush.24bcs10265@sst.scaler.com
- **Roll No:** 24BCS10265

## 1. Design

- **Queue:** Bounded SPSC circular buffer using `std::mutex` and `std::lock_guard`.
- **Producer:** Thread `t1` pushes 64-byte objects for one second.
- **Consumer:** Thread `t2` pops objects and verifies FIFO ordering.
- **Memory:** Preallocated circular buffer with capacity 1,024 objects.

## 2. Build & Run

```bash
clang++ -std=c++17 -O2 -Wall -Wextra -pthread spsc_queue.cpp -o main.out
./main.out
```

Run five trials:

```bash
for i in 1 2 3 4 5; do
    echo "===== RUN $i ====="
    ./main.out
done
```

## 3. Results (5 runs, 1 second each)

| Run | Pushed | Popped |
|---|---:|---:|
| 1 | 9,630,846 | 9,630,846 |
| 2 | 9,808,678 | 9,808,678 |
| 3 | 10,119,264 | 10,119,264 |
| 4 | 9,270,313 | 9,270,313 |
| 5 | 9,575,618 | 9,575,618 |
| **Average** | **9,680,944 / sec** | **9,680,944 / sec** |

All five runs passed FIFO correctness checks. Every object pushed was eventually popped without loss.

