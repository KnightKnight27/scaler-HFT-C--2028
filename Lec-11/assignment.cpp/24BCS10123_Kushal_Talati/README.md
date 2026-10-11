# Lec-11: SPSC queue with locks

Kushal Talati

```
email: "kushal.24bcs10123@sst.scaler.com"
roll_no: "24BCS10123"
```

Single producer / single consumer ring buffer. `t1` pushes 64 byte objects, `t2` pops them, main stops both after 1 second and joins them. Buffer is allocated once (memory pool), no `new`/`delete` on push/pop. Same queue is run with `std::mutex` and with a spinlock (`while (busy.exchange(true)) {}`).

## Build / run

```
make
./spsc_queue          # 5 trials, 4096 slots
```

Note: on macOS the mutex beat the spinlock. The spinlock is unfair, so the producer keeps re-grabbing it and spins on "full"; the mutex hands off to the waiting thread, so the two alternate.
