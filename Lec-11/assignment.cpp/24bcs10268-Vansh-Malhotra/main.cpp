#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

#include "spsc_queue.hpp"

using Clock = std::chrono::steady_clock;

constexpr std::size_t kCapacity = 1024;
constexpr int kRuns = 5;

struct Result {
    std::uint64_t pushed;
    std::uint64_t popped;
    double seconds;
    bool in_order;
};

template <typename Lock>
Result benchmark() {
    LockedRing<Packet, kCapacity, Lock> ring;
    std::atomic<bool> time_up{false};
    std::atomic<bool> producer_done{false};
    Result r{0, 0, 0.0, true};

    auto start = Clock::now();

    std::thread producer([&] {
        Packet p{};
        std::uint64_t n = 0;
        while (!time_up.load(std::memory_order_relaxed)) {
            p.seq = n;
            if (ring.try_push(p)) ++n;
        }
        r.pushed = n;
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        Packet p;
        std::uint64_t n = 0;
        bool in_order = true;
        for (;;) {
       
            bool done = producer_done.load(std::memory_order_acquire);
            if (ring.try_pop(p)) {
                in_order &= (p.seq == n);
                ++n;
            } else if (done) {
                break;
            }
        }
        r.popped = n;
        r.in_order = in_order;
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    time_up.store(true, std::memory_order_relaxed);
    producer.join();
    consumer.join();
    r.seconds = std::chrono::duration<double>(Clock::now() - start).count();
    return r;
}

template <typename Lock>
void report(const char* name) {
    std::printf("\n%s\n", name);
    std::printf("  run    pushed/s      popped/s      MB/s   check\n");
    for (int i = 1; i <= kRuns; ++i) {
        Result r = benchmark<Lock>();
        bool ok = r.in_order && r.pushed == r.popped;
        std::printf("  %d   %12.0f  %12.0f  %8.1f   %s\n", i,
                    r.pushed / r.seconds, r.popped / r.seconds,
                    r.popped * sizeof(Packet) / r.seconds / 1e6,
                    ok ? "ok" : "FAIL");
    }
}

int main() {
    report<std::mutex>("std::mutex");
    report<SpinLock>("SpinLock");
}
