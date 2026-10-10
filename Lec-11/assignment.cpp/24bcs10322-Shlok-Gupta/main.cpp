#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>

#include "spsc_queue.hpp"

using Clock = std::chrono::steady_clock;
constexpr std::size_t QUEUE_CAPACITY = 1024;
constexpr auto TEST_DURATION = std::chrono::seconds(1);

template <typename Lock>
void runBenchmark(const char* lockName) {
    SpscQueue<QUEUE_CAPACITY, Lock> queue;
    std::atomic<bool> stop{false};

    // Each counter is written by only one worker thread and read after join().
    std::uint64_t pushed = 0;
    std::uint64_t popped = 0;

    const auto start = Clock::now();

    std::thread producer([&]() {
        Object object{};

        while (!stop.load(std::memory_order_relaxed)) {
            object.data[0] = pushed;

            if (queue.push(object)) {
                ++pushed;
            }
        }
    });

    std::thread consumer([&]() {
        Object object{};

        while (!stop.load(std::memory_order_relaxed)) {
            if (queue.pop(object)) {
                ++popped;
            }
        }
    });

    std::this_thread::sleep_until(start + TEST_DURATION);
    stop.store(true, std::memory_order_relaxed);

    producer.join();
    consumer.join();

    const auto finish = Clock::now();
    const double elapsedSeconds =
        std::chrono::duration<double>(finish - start).count();

    const double pushedPerSecond = pushed / elapsedSeconds;
    const double poppedPerSecond = popped / elapsedSeconds;

    std::cout << "\n" << lockName << "\n"
              << "  Duration: " << std::fixed << std::setprecision(3)
              << elapsedSeconds << " seconds\n"
              << "  Successful pushes: " << pushed << "\n"
              << "  Successful pops:   " << popped << "\n"
              << "  Push throughput:   " << std::setprecision(0)
              << pushedPerSecond << " objects/second\n"
              << "  Pop throughput:    " << poppedPerSecond
              << " objects/second\n"
              << "  Queue capacity:    " << QUEUE_CAPACITY << " objects\n";
}

int main() {
    std::cout << "SPSC Queue Throughput Benchmark\n"
              << "Object size: 64 bytes\n"
              << "Each test runs for approximately one second.\n";

    runBenchmark<std::mutex>("Lock: std::mutex");
    runBenchmark<SpinLock>("Lock: SpinLock");

    std::cout << "\nNote: results depend on your CPU, compiler, power mode, "
                 "and background processes.\n";
    return 0;
}
