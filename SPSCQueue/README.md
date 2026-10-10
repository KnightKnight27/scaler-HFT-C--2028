## Student details
- email: jangam.24bcs10212@sst.scaler.com
- name: Jangam Rohan


## SPSC Queue

A fixed-size ring buffer protected by `std::mutex`. The benchmark uses one producer, one consumer, and 64-byte events. It runs for one second by default; an optional positive argument changes the duration in milliseconds.

Please note that this is not a lock-free implementation, and thus is not very good performance wise.
The actual SPSCQueue data structure is in `spscqueue.h`.
`main.cpp` only contains code that uses that class to perform as many push/pops as possible under the provided constraints.

### Build with CMake

```bash
cmake -S . -B build
cmake --build build --config Release
```

```bash
./build/SPSCQueue 3000
```

orrrr

### Build directly with g++

```bash
g++ -std=c++17 -O2 -Wall -Wextra -pthread main.cpp -o SPSCQueue
./SPSCQueue
./SPSCQueue 3000
```

The queue also has `front`, `back`, `size`, `empty`, `full`, and `clear` methods. `front` and `back` copy the item to an output argument while the mutex is held, so they do not return a reference after unlocking.


# Future improvement plans
- A lockfree impl would be better