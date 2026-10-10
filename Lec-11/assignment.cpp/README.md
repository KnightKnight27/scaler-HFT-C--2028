# lec 11 - SPSC queue

name: Pateel Deepesh Kumar Reddy.
rollno: 24bcs10477

Spinlock based single producer single consumer queue. One producer thread pushes
64 byte messages, one consumer thread pops them. Both joined before exit.

```
g++ -std=c++17 -O2 -pthread main.cpp -o spsc
./spsc
```

## per second specs

4096 slots, 64 byte message, g++ 16.2, `-O2`, intel core ultra 5 125h, 14 cores.

| build | pushes | pops |
|---|---|---|
| `-O2` | 3.2M - 5.0M / sec | same |
| `-O0` | 2.0M - 2.6M / sec | same |

the loop runs for 1 sec and the queue is kept full the whole time, so every run is
saturated. spread is wide because other stuff was running on the box, load avg was
around 4. on an idle machine it sits closer to 5M-6M.

## notes

- `mMask = size - 1`, so capacity has to be a power of 2 (asserted in the ctor).
  not a power of 2 silently corrupts the data.
- `& mMask` instead of `% size`, saves the modulo.
- `yield()` when the queue is full or empty. spinning on the lock made it slower.
- `static_assert(sizeof(Msg) == 64)` so the message size cannot drift.

`spsc_queue.h` has the queue, `main.cpp` has the benchmark. clean under
`-Wall -Wextra`, and thread/asan/ubsan sanitizers are clean.