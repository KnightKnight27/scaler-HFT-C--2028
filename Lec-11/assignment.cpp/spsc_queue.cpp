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
#include "spsc_queue_mutex.cpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

struct alignas(64) Msg64 {
    char data[64];
};
static_assert(sizeof(Msg64) == 64, "Msg64 must be 64 bytes");
static_assert(alignof(Msg64) == 64, "Msg64 must be 64-byte aligned");

int main() {
    const std::size_t kCap = 1 << 16;
    const auto kWindow = std::chrono::seconds(1);
    MutexSPSCQueue<Msg64> q(kCap);

    std::atomic<long> pushed{0}, popped{0};
    std::atomic<bool> stop{false};

    Msg64 m{};
    m.data[0] = 42;

    auto t0 = std::chrono::steady_clock::now();
    auto deadline = t0 + kWindow;

    std::thread t1([&] {
        long local = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            if (q.push(m)) ++local;
            else std::this_thread::yield();
        }
        pushed.store(local, std::memory_order_relaxed);
        auto end2 = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (std::chrono::steady_clock::now() < end2) {
            if (popped.load(std::memory_order_relaxed) >= local) break;
            std::this_thread::yield();
        }
        stop.store(true, std::memory_order_release);
    });

    std::thread t2([&] {
        long local = 0;
        while (!stop.load(std::memory_order_acquire)) {
            bool ok = q.pop([&](Msg64 &v) {
                (void)v;
                ++local;
            });
            if (!ok) std::this_thread::yield();
            if ((local & 0x3FFF) == 0) popped.store(local, std::memory_order_relaxed);
        }
        while (q.pop([&](Msg64 &v) { (void)v; ++local; })) {}
        popped.store(local, std::memory_order_relaxed);
    });

    t1.join();
    t2.join();

    auto t1end = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1end - t0).count();
    long p = pushed.load(), c = popped.load();
    std::cout << "cap=" << kCap << " window=1s total_wall=" << secs << "s\n";
    std::cout << "pushed=" << p << " popped=" << c << "\n";
    std::cout << "push/sec~" << (long)(p / secs) << " pop/sec~" << (long)(c / secs) << "\n";
    std::cout << "note: mutex SPSC, 64B objects, t1+t2 joined\n";
    return (c > 0 && c <= p) ? 0 : 1;
}
