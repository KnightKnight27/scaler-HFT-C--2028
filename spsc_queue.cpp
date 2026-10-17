// Lock-based SPSC queue benchmark: 64-byte objects, two joined worker threads.
#include "spsc_queue.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

namespace {
using Clock = std::chrono::steady_clock;

struct Result {
    double elapsed_seconds;
    std::uint64_t completed_in_window;
    std::uint64_t produced;
    std::uint64_t consumed;
    std::uint64_t errors;
};

template <class Lock> Result benchmark(std::size_t capacity, double seconds) {
    assignment::SPSCQueue<Lock> queue(capacity);
    std::atomic<unsigned> ready{0};
    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<bool> producer_done{false};
    alignas(64) std::atomic<std::uint64_t> completed{0};
    std::uint64_t produced = 0;
    std::uint64_t consumed = 0;
    std::uint64_t errors = 0;

    const auto wait_for_start = [&] {
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    };

    std::thread producer([&] {
        wait_for_start();
        while (!stop.load(std::memory_order_relaxed)) {
            const auto object = assignment::make_object(produced);
            if (queue.try_push(object)) {
                ++produced;
            } else {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        wait_for_start();
        assignment::Object value;
        const auto consume = [&] {
            if (!assignment::valid_object(value, consumed)) {
                ++errors;
            }
            ++consumed;
            completed.store(consumed, std::memory_order_relaxed);
        };
        while (!producer_done.load(std::memory_order_acquire)) {
            if (queue.try_pop(value)) {
                consume();
            } else {
                std::this_thread::yield();
            }
        }
        // Recheck after observing producer_done: all final pushes are published.
        while (queue.try_pop(value)) {
            consume();
        }
    });

    while (ready.load(std::memory_order_acquire) != 2) {
        std::this_thread::yield();
    }
    const auto begin = Clock::now();
    start.store(true, std::memory_order_release);
    std::this_thread::sleep_until(begin + std::chrono::duration<double>(seconds));
    const auto window_count = completed.load(std::memory_order_relaxed);
    const auto end = Clock::now();
    stop.store(true, std::memory_order_relaxed);
    producer.join();
    consumer.join();
    return {std::chrono::duration<double>(end - begin).count(), window_count, produced, consumed,
            errors};
}

std::size_t positive_size(const char *text) {
    const std::string input(text);
    if (input.empty() || input.find_first_not_of("0123456789") != std::string::npos) {
        throw std::invalid_argument("expected a positive integer");
    }
    const auto value = std::stoull(input);
    if (value == 0 || value > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument("integer is outside the supported range");
    }
    return static_cast<std::size_t>(value);
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc > 5) {
            throw std::invalid_argument("too many arguments");
        }
        const std::string lock = argc > 1 ? argv[1] : "mutex";
        std::size_t parsed = 0;
        const std::string duration = argc > 2 ? argv[2] : "1";
        const double seconds = std::stod(duration, &parsed);
        if (parsed != duration.size() || !std::isfinite(seconds) || seconds <= 0 ||
            seconds > 3600) {
            throw std::invalid_argument("seconds must be finite and in (0, 3600]");
        }
        const auto capacity = argc > 3 ? positive_size(argv[3]) : 1024;
        const auto runs = argc > 4 ? positive_size(argv[4]) : 5;
        if (lock != "mutex" && lock != "spin") {
            throw std::invalid_argument("lock must be mutex or spin");
        }
        std::vector<double> rates;
        std::cout
            << "lock,run,object_bytes,capacity,elapsed_seconds,completed_in_window,"
               "transfers_per_second,produced_including_drain,consumed_including_drain,errors\n";
        for (std::size_t run = 1; run <= runs; ++run) {
            const auto result = lock == "mutex"
                                    ? benchmark<std::mutex>(capacity, seconds)
                                    : benchmark<assignment::SpinLock>(capacity, seconds);
            const double rate = result.completed_in_window / result.elapsed_seconds;
            rates.push_back(rate);
            std::cout << lock << ',' << run << ",64," << capacity << ',' << std::fixed
                      << std::setprecision(6) << result.elapsed_seconds << ','
                      << result.completed_in_window << ',' << std::setprecision(2) << rate << ','
                      << result.produced << ',' << result.consumed << ',' << result.errors << '\n';
            if (result.produced != result.consumed || result.errors != 0) {
                std::cerr << "FAIL: loss, duplication, FIFO violation, or payload corruption\n";
                return 1;
            }
        }
        std::sort(rates.begin(), rates.end());
        const auto middle = rates.size() / 2;
        const double median =
            rates.size() % 2 ? rates[middle] : (rates[middle - 1] + rates[middle]) / 2;
        std::cerr << "median completed 64-byte transfers/s: " << std::fixed << std::setprecision(2)
                  << median << '\n';
    } catch (const std::exception &error) {
        std::cerr << "Error: " << error.what()
                  << "\nUsage: spsc_queue [mutex|spin] [seconds=1] [capacity=1024] [runs=5]\n";
        return 2;
    }
}
