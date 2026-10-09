// WRITE AN SPSC QUEUE 
// SPINLOCK ( WHILE LOOP) OR STD::MUTEX 
// t1.join()  t2.join()
// producer consumer to push objects and pop objects 
//
// you need to figure out a way that with locks how many 
// 64 byte objects can u push and pop in 1 second
//  raise a git PR for the same 
//  add readme for ur per second specs 
//  feel free to add worst code qaulity :)
//
//
// ^^ MEMORY POOL ^^

// SPSC Assignment - Rohan Ranjan - 10428
//
// how many 64 byte objects can we push and pop in 1 second with a lock?
// t1 = producer, t2 = consumer, main sleeps 1 sec then stops them
//
// build: g++ -std=c++17 -O2 -pthread spsc_queue.cpp -o spsc_queue
// run:   ./spsc_queue        (or ./spsc_queue 10 for 10 runs each)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "spsc_queue.hpp"

// the object we push, exactly 64 bytes (one cache line)
struct alignas(64) Order {
    uint64_t seq;
    uint64_t price;
    uint64_t qty;
    char     symbol[40];
};
static_assert(sizeof(Order) == 64, "Order must be exactly 64 bytes");

struct Result {
    uint64_t pushed;
    uint64_t popped;
    double   secs;
    bool     ok;
};

// one run: both threads go for 1 second
template <typename Queue>
Result run_once(size_t capacity, std::chrono::milliseconds dur) {
    Queue q(capacity);
    std::atomic<bool> start{false}, stop{false};
    uint64_t pushed = 0, popped = 0;
    bool ok = true;

    // t1: producer
    std::thread t1([&] {
        Order o{};
        std::strcpy(o.symbol, "AAPL");
        uint64_t seq = 0;
        while (!start.load(std::memory_order_acquire)) CPU_RELAX();
        while (!stop.load(std::memory_order_relaxed)) {
            o.seq = seq; o.price = seq * 3; o.qty = seq & 1023;
            if (q.push(o)) ++seq;
        }
        pushed = seq;
    });

    // t2: consumer, also checks every object comes out in order
    std::thread t2([&] {
        Order o;
        uint64_t expect = 0;
        while (!start.load(std::memory_order_acquire)) CPU_RELAX();
        while (!stop.load(std::memory_order_relaxed)) {
            if (q.pop(o)) {
                if (o.seq != expect || o.price != expect * 3) ok = false;
                ++expect;
            }
        }
        popped = expect;
    });

    // start both together, wait 1 sec, then stop
    auto t0 = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(dur);
    stop.store(true, std::memory_order_relaxed);
    t1.join();
    t2.join();
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return {pushed, popped, secs, ok};
}

// run it a few times and print min / median / max
template <typename Queue>
void bench(const char* name, size_t capacity, int runs) {
    std::vector<double> rates;
    bool all_ok = true;
    for (int i = 0; i < runs; ++i) {
        Result r = run_once<Queue>(capacity, std::chrono::milliseconds(1000));
        all_ok &= r.ok;
        rates.push_back(r.popped / r.secs);
    }
    std::sort(rates.begin(), rates.end());
    double med = rates[rates.size() / 2];
    std::printf("%-22s min %8.2f M/s | median %8.2f M/s | max %8.2f M/s | %7.2f GB/s | fifo %s\n",
                name, rates.front() / 1e6, med / 1e6, rates.back() / 1e6,
                med * sizeof(Order) / 1e9, all_ok ? "OK" : "BROKEN");
}

int main(int argc, char** argv) {
    size_t capacity = 1 << 16;               // 65536 slots * 64 B = 4 MB pool
    int    runs     = argc > 1 ? std::atoi(argv[1]) : 5;

    std::printf("SPSC queue, 64-byte objects, capacity %zu, %d x 1s runs each\n", capacity, runs);
    std::printf("numbers = objects pushed AND popped (consumed) per second\n\n");
    bench<LockedSPSCQueue<Order, SpinLock>>("spinlock", capacity, runs);
    bench<LockedSPSCQueue<Order, std::mutex>>("std::mutex", capacity, runs);
    bench<LockFreeSPSCQueue<Order>>("lock-free (baseline)", capacity, runs);
    return 0;
}
