# lec 11 spsc queue readout

made spsc queue with array memory pool of 8192. 64 byte struct. one producer thread and one consumer thread, t1.join t2.join. tested both mutex and spinlock (while loop) for 1 sec.

how to run:
```
g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc_bench && ./spsc_bench
```

## my specs / output

- CPU: AMD Ryzen 5 6600H, 12 threads, 14 GB RAM
- Compiler: g++ 16.2.1, `-O2 -std=c++17`
- `mutex:    pushed=11672195 popped=11672135 / sec`
- `spinlock: pushed=7244610 popped=7243093 / sec`
- mutex won on my laptop (~11.6M/s vs ~7.2M/s)

numbers will change a bit every run, just re-run and paste ur output.
