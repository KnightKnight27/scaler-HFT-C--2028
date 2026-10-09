# SPSC Queue Benchmarks (Lec-11 Assignment)

Implementation of SPSC queue for 64-byte objects using:
1. `spsc_spinlock.cpp` - Spinlock implementation with busy-wait `while` loop using `std::atomic_flag`.
2. `spsc_mutex.cpp` - Standard library `std::mutex` with manual lock/unlock.
3. `spsc_queue.cpp` - Combined version with lock switch.

## Specs (1-second run):
- **Object Size**: 64 bytes (`sizeof(Object64) == 64`)
- **Queue Buffer Capacity**: 1024

### Results (`-O0`):
- **Spinlock (`spsc_spinlock.cpp`)**:
  - Pushed in 1s: 3,040,491 objects (~3.04M)
  - Popped in 1s: 3,040,490 objects (~3.04M)
- **std::mutex (`spsc_mutex.cpp`)**:
  - Pushed in 1s: 3,526,892 objects (~3.53M)
  - Popped in 1s: 3,526,867 objects (~3.53M)


### How to compile and run:
```bash
# Spinlock
g++ -O0 spsc_spinlock.cpp -o main_spin
./main_spin

# std::mutex
g++ -O0 spsc_mutex.cpp -o main_mutex
./main_mutex
```
