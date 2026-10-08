#include "object64.hpp"
#include "spsc_queue.hpp"

#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <system_error>

using Clock = std::chrono::steady_clock;

// Both workers wait here before main publishes the common timing window.
class StartGate {
public:
    Clock::time_point wait() {
        std::unique_lock<std::mutex> guard(mutex_);
        ++ready_;
        cv_.notify_all();
        cv_.wait(guard, [&] { return started_; });
        return deadline_;
    }

    Clock::time_point start(double seconds) {
        std::unique_lock<std::mutex> guard(mutex_);
        cv_.wait(guard, [&] { return ready_ == 2; });
        const auto start = Clock::now();
        deadline_ = start + std::chrono::duration_cast<Clock::duration>(
                                std::chrono::duration<double>(seconds));
        started_ = true;
        cv_.notify_all();
        return start;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int ready_ = 0;
    bool started_ = false;
    Clock::time_point deadline_{};
};

struct Result {
    std::uint64_t produced = 0;
    std::uint64_t consumed = 0;
    std::uint64_t in_window = 0;
    std::uint64_t full_retries = 0;
    std::uint64_t empty_retries = 0;
    std::uint64_t errors = 0;
    double elapsed = 0;
};

template <typename Lock>
Result run(double seconds, std::size_t capacity) {
    SPSCQueue<Object64, Lock> queue(capacity);
    StartGate gate;
    std::atomic<bool> producer_done{false};
    Result result;

    std::thread producer([&] {
        const auto deadline = gate.wait();
        auto value = make_object(0);
        while (Clock::now() < deadline) {
            if (queue.try_push(value)) {
                ++result.produced;
                value = make_object(result.produced);
            } else {
                ++result.full_retries;
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        const auto deadline = gate.wait();
        Object64 value;
        while (true) {
            bool popped = queue.try_pop(value);
            if (!popped) {
                ++result.empty_retries;
                if (producer_done.load(std::memory_order_acquire)) {
                    // The producer might have published its last item between
                    // our empty observation and the done flag. Check once more.
                    popped = queue.try_pop(value);
                    if (!popped) {
                        break;
                    }
                } else {
                    std::this_thread::yield();
                    continue;
                }
            }
            // Timestamp immediately after pop. Payload validation still adds
            // work to this benchmark, but drain work never increases this count.
            if (Clock::now() <= deadline) {
                ++result.in_window;
            }
            if (!valid_object(value, result.consumed)) {
                ++result.errors;
            }
            ++result.consumed;
        }
    });

    const auto start = gate.start(seconds);
    producer.join();
    consumer.join();
    result.elapsed = std::chrono::duration<double>(Clock::now() - start).count();
    return result;
}

std::size_t positive_integer(const char* text) {
    const std::string input(text);
    std::size_t value = 0;
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() || value == 0) {
        throw std::invalid_argument("Runs and capacity must be positive integers");
    }
    return value;
}

template <typename Lock>
bool benchmark(const char* lock_name, double seconds, std::size_t runs, std::size_t capacity) {
    // One short unreported warm-up per variant.
    const auto warmup = run<Lock>(0.1, capacity);
    bool good = warmup.errors == 0 && warmup.produced == warmup.consumed;
    for (std::size_t trial = 1; trial <= runs; ++trial) {
        const auto r = run<Lock>(seconds, capacity);
        good = good && r.errors == 0 && r.produced == r.consumed;
        std::cout << lock_name << ',' << trial << ',' << capacity << ',' << sizeof(Object64)
                  << ',' << seconds << ',' << r.in_window << ',' << r.in_window / seconds
                  << ',' << r.produced << ',' << r.consumed << ',' << r.consumed - r.in_window
                  << ',' << r.elapsed << ',' << r.full_retries << ',' << r.empty_retries
                  << ',' << r.errors << '\n';
    }
    return good;
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << "Usage: " << argv[0]
                      << " [seconds=1] [runs=5] [capacity=1024] [mutex|spin|both]\n";
            return EXIT_SUCCESS;
        }
        if (argc > 5) {
            throw std::invalid_argument("Too many arguments; use --help");
        }
        std::size_t parsed_chars = 0;
        const std::string duration_arg = argc > 1 ? argv[1] : "1";
        const double seconds = std::stod(duration_arg, &parsed_chars);
        // A practical bound also keeps chrono's integer conversion in range.
        if (parsed_chars != duration_arg.size() || !std::isfinite(seconds) ||
            seconds < 0.001 || seconds > 60) {
            throw std::invalid_argument("Seconds must be between 0.001 and 60");
        }
        const auto runs = argc > 2 ? positive_integer(argv[2]) : 5;
        const auto capacity = argc > 3 ? positive_integer(argv[3]) : 1024;
        const std::string mode = argc > 4 ? argv[4] : "both";
        if (mode != "mutex" && mode != "spin" && mode != "both") {
            throw std::invalid_argument("Lock must be mutex, spin, or both");
        }
        if (runs > 1000 || capacity > 1048576) {
            throw std::invalid_argument("Maximum runs: 1000; maximum capacity: 1048576");
        }

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "lock,trial,capacity,object_bytes,window_seconds,completed_in_window,"
                     "objects_per_second,produced_total,consumed_total,drained_after_window,"
                     "elapsed_including_drain_seconds,full_retries,empty_retries,payload_errors\n";
        bool good = true;
        if (mode == "mutex" || mode == "both") {
            good = benchmark<std::mutex>("mutex", seconds, runs, capacity) && good;
        }
        if (mode == "spin" || mode == "both") {
            good = benchmark<SpinLock>("spin", seconds, runs, capacity) && good;
        }
        return good ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
