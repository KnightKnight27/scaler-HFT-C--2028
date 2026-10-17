#include "spsc_queue.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <thread>

namespace {

constexpr std::size_t kQueueCapacity = 1024;
constexpr auto kBenchmarkDuration = std::chrono::seconds(1);

struct BenchmarkResult {
    std::uint64_t pushed;
    std::uint64_t popped;
    double elapsed_seconds;
    bool correct;
};

template <typename Lock>
BenchmarkResult run_benchmark() {
    SpscQueue<Lock, kQueueCapacity> queue;
    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<bool> producer_done{false};
    std::uint64_t pushed = 0;
    std::uint64_t popped = 0;
    bool sequence_is_valid = true;

    std::thread producer([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        Item item{};
        std::uint64_t sequence = 0;
        while (!stop.load(std::memory_order_acquire)) {
            std::memcpy(item.data, &sequence, sizeof(sequence));
            if (queue.push(item)) {
                ++sequence;
                ++pushed;
            } else {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        Item item{};
        std::uint64_t expected_sequence = 0;
        for (;;) {
            if (queue.pop(item)) {
                std::uint64_t sequence = 0;
                std::memcpy(&sequence, item.data, sizeof(sequence));
                if (sequence != expected_sequence) {
                    sequence_is_valid = false;
                }
                ++expected_sequence;
                ++popped;
            } else if (producer_done.load(std::memory_order_acquire)) {
                break;
            } else {
                std::this_thread::yield();
            }
        }
    });

    const auto start_time = std::chrono::steady_clock::now();
    const auto deadline = start_time + kBenchmarkDuration;
    start.store(true, std::memory_order_release);
    std::this_thread::sleep_until(deadline);
    stop.store(true, std::memory_order_release);
    const auto end_time = std::chrono::steady_clock::now();

    producer.join();
    consumer.join();

    const double elapsed_seconds =
        std::chrono::duration<double>(end_time - start_time).count();
    const bool counts_are_valid = pushed >= popped && pushed == popped;
    return {pushed, popped, elapsed_seconds,
            sequence_is_valid && counts_are_valid && popped > 0};
}

template <typename Lock>
bool print_result(const char* variant) {
    const BenchmarkResult result = run_benchmark<Lock>();
    const double ops_per_second = result.popped / result.elapsed_seconds;
    const double megabytes_per_second = ops_per_second * sizeof(Item) / 1'000'000.0;

    std::cout << std::left << std::setw(12) << variant << " | "
              << std::right << result.pushed << " | " << result.popped << " | "
              << std::fixed << std::setprecision(2) << ops_per_second << " | "
              << megabytes_per_second << '\n';

    if (!result.correct) {
        std::cerr << variant
                  << " queue failed sequence or pushed/popped count validation\n";
        return false;
    }
    return true;
}

}  // namespace

int main() {
    std::cout << "Variant      | Items pushed | Items popped | Ops/sec | MB/s\n";
    const bool mutex_ok = print_result<MutexLock>("mutex");
    const bool spinlock_ok = print_result<SpinLock>("spinlock");
    return mutex_ok && spinlock_ok ? 0 : 1;
}
