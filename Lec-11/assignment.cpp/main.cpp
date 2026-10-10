#include "spsc_queue.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

constexpr auto benchmark_duration = std::chrono::seconds(1);
constexpr int trial_count = 3;

struct TrialResult {
    std::uint64_t pushes;
    std::uint64_t pops;
};

template <typename Lock>
TrialResult run_trial()
{
    SPSCQueue<Lock> queue;
    std::atomic<bool> producer_done{false};
    std::uint64_t pushes = 0;
    std::uint64_t pops = 0;

    std::thread t1([&] {
        const Item item{};
        const auto start_time = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start_time < benchmark_duration) {
            if (queue.push(item)) {
                ++pushes;
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread t2([&] {
        Item item{};
        for (;;) {
            if (queue.pop(item)) {
                ++pops;
            } else if (producer_done.load(std::memory_order_acquire)) {
                break;
            }
        }
    });

    t1.join();
    t2.join();
    return {pushes, pops};
}

template <typename Lock>
double report_lock(const char* name)
{
    std::array<double, trial_count> rates{};
    std::cout << name << " lock\n";
    for (int trial = 0; trial < trial_count; ++trial) {
        const TrialResult result = run_trial<Lock>();
        if (result.pushes != result.pops) {
            throw std::runtime_error("Correctness check failed: pushes != pops");
        }

        rates[trial] = static_cast<double>(result.pushes) /
                       std::chrono::duration<double>(benchmark_duration).count();
        std::cout << "  Trial " << trial + 1 << ": " << std::fixed
                  << std::setprecision(0) << rates[trial]
                  << " objects/sec (pushes=" << result.pushes
                  << ", pops=" << result.pops << ")\n";
    }

    const double average = (rates[0] + rates[1] + rates[2]) / trial_count;
    std::cout << "  Average: " << std::fixed << std::setprecision(0)
              << average << " objects/sec\n\n";
    return average;
}

} // namespace

int main()
{
    try {
        report_lock<MutexLock>("Mutex");
        report_lock<SpinLock>("Spin");
    } catch (const std::exception& error) {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
