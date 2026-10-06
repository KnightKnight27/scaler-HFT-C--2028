#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <iomanip>
#include <string>

// Include all 4 modular SPSC queue implementations
#include "LockSPSC.cpp"       // Mutex-based
#include "SpinLockSPSC.cpp"   // Spinlock-based
#include "spsc_queue.cpp"     // Naive Lock-Free atomic (contains Frame struct)
#include "OptimizedSPSC.cpp"  // Cache-Aware Optimized Lock-Free

template <typename QueueType>
void run_independent_test(const std::string& test_name, size_t queue_capacity, size_t duration_seconds) {
    QueueType queue(queue_capacity);

    std::atomic<bool> start_signal{false};
    std::atomic<bool> stop_signal{false};
    std::atomic<uint64_t> total_popped{0};

    // Producer Thread
    std::thread producer([&]() {
        Frame item{1, 100000, "MARKET_DATA_PAYLOAD_64_BYTES"};

        while (!start_signal.load(std::memory_order_relaxed)) {
            // Spin-wait for clock sync
        }

        while (!stop_signal.load(std::memory_order_relaxed)) {
            if (!queue.push(item)) {
#if defined(__x86_64__) || defined(_M_X64)
                __builtin_ia32_pause();
#endif
            }
        }
    });

    // Consumer Thread
    std::thread consumer([&]() {
        Frame item;
        uint64_t local_pop_count = 0;

        while (!start_signal.load(std::memory_order_relaxed)) {
            // Spin-wait for clock sync
        }

        while (!stop_signal.load(std::memory_order_relaxed)) {
            if (queue.pop(item)) {
                local_pop_count++;
            } else {
#if defined(__x86_64__) || defined(_M_X64)
                __builtin_ia32_pause();
#endif
            }
        }
        total_popped.store(local_pop_count, std::memory_order_relaxed);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto start_time = std::chrono::high_resolution_clock::now();
    start_signal.store(true, std::memory_order_relaxed);

    std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));
    stop_signal.store(true, std::memory_order_relaxed);

    producer.join();
    consumer.join();
    auto end_time = std::chrono::high_resolution_clock::now();

    double elapsed_seconds = std::chrono::duration<double>(end_time - start_time).count();
    uint64_t total_ops = total_popped.load();
    double mops = (static_cast<double>(total_ops) / elapsed_seconds) / 1e6;
    double avg_latency_ns = (elapsed_seconds * 1e9) / static_cast<double>(total_ops);

    std::cout << std::left << std::setw(32) << test_name
              << " | " << std::setw(14) << total_ops
              << " | " << std::setw(12) << std::fixed << std::setprecision(2) << mops
              << " | " << std::setw(14) << std::setprecision(2) << avg_latency_ns << "\n";
}

int main() {
    constexpr size_t CAPACITY = 1024;    // Power-of-two buffer size
    constexpr size_t TEST_DURATION = 1; // Execution window in seconds

    std::cout << "===================================================================================\n";
    std::cout << "               SPSC QUEUE PERFORMANCE EVOLUTION BENCHMARK                          \n";
    std::cout << "===================================================================================\n";
    std::cout << std::left << std::setw(32) << "Queue Implementation"
              << " | " << std::setw(14) << "Total Ops"
              << " | " << std::setw(12) << "Mops/sec"
              << " | " << std::setw(14) << "Avg Latency(ns)" << "\n";
    std::cout << "-----------------------------------------------------------------------------------\n";

    run_independent_test<LockSPSC<Frame>>("1. LockSPSC (std::mutex)", CAPACITY, TEST_DURATION);
    run_independent_test<SpinLockSPSC<Frame>>("2. SpinLockSPSC (Spinlock)", CAPACITY, TEST_DURATION);
    run_independent_test<SPSC<Frame>>("3. SPSC (Naive Lock-Free)", CAPACITY, TEST_DURATION);
    run_independent_test<OptimizedSPSC<Frame>>("4. OptimizedSPSC (Cache-Aware)", CAPACITY, TEST_DURATION);

    std::cout << "===================================================================================\n";

    return 0;
}
