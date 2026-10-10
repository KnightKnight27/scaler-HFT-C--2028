#include "LockSPSC.hpp"
#include "SpinLockSPSC.hpp"
#include "spsc_queue.hpp"
#include "OptimizedSPSC.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>
#include <string>

struct Object64 {
    std::uint64_t sequence{};
    char payload[56]{};
};
static_assert(sizeof(Object64) == 64, "Object64 must be exactly 64 bytes");

constexpr std::size_t kCapacity = 1024;
constexpr std::uint64_t kItems = 2'000'000;

struct Result {
    std::string name;
    std::uint64_t objects{};
    double seconds{};
    double mops{};
    double ns_per_object{};
    bool correct{};
};

template<class Queue>
Result run_benchmark(const char* name) {
    Queue queue;
    std::atomic<bool> start{false};
    std::atomic<bool> producer_done{false};
    std::atomic<bool> mismatch{false};
    std::atomic<std::uint64_t> consumed{0};

    std::thread producer([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::uint64_t i = 0; i < kItems;) {
            Object64 item{};
            item.sequence = i;
            if (queue.push(item)) ++i;
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        start.store(true, std::memory_order_release);
        std::uint64_t expected = 0;
        Object64 item{};
        while (expected < kItems) {
            if (queue.pop(item)) {
                if (item.sequence != expected) mismatch.store(true, std::memory_order_relaxed);
                ++expected;
            } else if (producer_done.load(std::memory_order_acquire)) {
                break;
            }
        }
        consumed.store(expected, std::memory_order_relaxed);
    });

    // Both worker threads exist before the timed interval begins.
    const auto begin = std::chrono::steady_clock::now();
    producer.join();
    consumer.join();
    const auto end = std::chrono::steady_clock::now();

    const double seconds = std::chrono::duration<double>(end - begin).count();
    const auto count = consumed.load(std::memory_order_relaxed);
    return {name, count, seconds,
            seconds > 0 ? static_cast<double>(count) / seconds / 1e6 : 0.0,
            count > 0 ? seconds * 1e9 / static_cast<double>(count) : 0.0,
            count == kItems && !mismatch.load(std::memory_order_relaxed)};
}

int main() {
    std::cout << "SPSC benchmark | C++20 | object_size=" << sizeof(Object64)
              << " bytes | capacity=" << kCapacity
              << " | transfers=" << kItems << "\n";
    std::cout << "One operation = one object successfully pushed and then popped.\n\n";
    std::cout << std::left << std::setw(24) << "Implementation"
              << std::right << std::setw(14) << "Objects"
              << std::setw(16) << "M objects/s"
              << std::setw(18) << "ns/transfer"
              << std::setw(12) << "Verified" << '\n';

    const auto r1 = run_benchmark<LockSPSC<Object64, kCapacity>>("LockSPSC");
    const auto r2 = run_benchmark<SpinLockSPSC<Object64, kCapacity>>("SpinLockSPSC");
    const auto r3 = run_benchmark<SPSCQueue<Object64, kCapacity>>("Naive Lock-Free");
    const auto r4 = run_benchmark<OptimizedSPSC<Object64, kCapacity>>("Optimized Lock-Free");

    const Result results[] = {r1, r2, r3, r4};
    for (const auto& r : results) {
        std::cout << std::left << std::setw(24) << r.name
                  << std::right << std::setw(14) << r.objects
                  << std::setw(16) << std::fixed << std::setprecision(2) << r.mops
                  << std::setw(18) << std::setprecision(2) << r.ns_per_object
                  << std::setw(12) << (r.correct ? "yes" : "NO") << '\n';
    }

    const double baseline = r1.mops;
    std::cout << "\nRelative throughput vs mutex baseline:\n";
    for (const auto& r : results) {
        std::cout << "  " << r.name << ": "
                  << (baseline > 0 ? r.mops / baseline : 0.0) << "x\n";
    }

    for (const auto& r : results) {
        if (!r.correct) {
            std::cerr << "ERROR: correctness verification failed for " << r.name << '\n';
            return 1;
        }
    }
    return 0;
}
