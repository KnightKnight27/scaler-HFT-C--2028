// Build: c++ -std=c++20 -O2 -pthread spsc_queue.cpp -o spsc_queue
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

struct alignas(64) Order {
    std::uint64_t sequence{};
    std::array<std::uint64_t, 7> payload{};
};
static_assert(sizeof(Order) == 64);

Order makeOrder(std::uint64_t sequence) {
    Order order;
    order.sequence = sequence;
    order.payload.fill(sequence);
    return order;
}

bool matches(const Order& order, std::uint64_t expected) {
    return order.sequence == expected &&
           std::all_of(order.payload.begin(), order.payload.end(),
                       [expected](std::uint64_t value) { return value == expected; });
}

class SpinLock {
public:
    void lock() noexcept {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // Read while occupied instead of repeatedly modifying the flag.
            while (flag_.test(std::memory_order_relaxed)) {}
        }
    }
    void unlock() noexcept { flag_.clear(std::memory_order_release); }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

template <typename Lock, std::size_t Capacity = 1024>
class SPSCQueue {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a nonzero power of two");
public:
    bool push(const Order& order) {
        std::lock_guard<Lock> guard(lock_);
        if (write_ - read_ == Capacity) return false;
        pool_[write_ & (Capacity - 1)] = order;
        ++write_;
        return true;
    }

    bool pop(Order& order) {
        std::lock_guard<Lock> guard(lock_);
        if (write_ == read_) return false;
        order = pool_[read_ & (Capacity - 1)];
        ++read_;
        return true;
    }

private:
    // Fixed memory pool: all slots exist before the threads start, and are reused.
    std::array<Order, Capacity> pool_{};
    std::size_t write_ = 0;
    std::size_t read_ = 0;
    Lock lock_;
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Lock>
void testQueue() {
    SPSCQueue<Lock, 4> queue;
    Order out;
    require(!queue.pop(out), "Empty queue accepted a pop");
    for (std::uint64_t base = 0; base < 400; base += 4) {
        for (std::uint64_t i = 0; i < 4; ++i)
            require(queue.push(makeOrder(base + i)), "Push failed before full");
        require(!queue.push(makeOrder(999)), "Full queue accepted a push");
        for (std::uint64_t i = 0; i < 4; ++i)
            require(queue.pop(out) && matches(out, base + i), "FIFO/payload mismatch");
        require(!queue.pop(out), "Drained queue is not empty");
    }
    SPSCQueue<Lock, 1> single;
    require(single.push(makeOrder(7)) && !single.push(makeOrder(8)), "Capacity-one full check");
    require(single.pop(out) && matches(out, 7) && !single.pop(out), "Capacity-one pop check");

    // Small capacity forces many wraparounds under contention.
    constexpr std::uint64_t count = 100000;
    bool valid = true;
    std::thread producer([&] {
        for (std::uint64_t i = 0; i < count; ++i) {
            const Order order = makeOrder(i);
            while (!queue.push(order)) {}
        }
    });
    std::thread consumer([&] {
        Order order;
        for (std::uint64_t i = 0; i < count; ++i) {
            while (!queue.pop(order)) {}
            valid = matches(order, i) && valid;
        }
    });
    producer.join();
    consumer.join();
    require(valid && !queue.pop(out), "Concurrent FIFO/payload check failed");
}

struct Result {
    std::uint64_t pushed = 0, popped = 0, full = 0, empty = 0, remaining = 0;
    double seconds = 0;
    bool valid = true;
};

template <typename Lock>
Result benchmark() {
    SPSCQueue<Lock> queue;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false}, stop{false};
    Result result;
    std::thread t1([&] {
        std::uint64_t pushed = 0, full = 0;
        Order order = makeOrder(0);
        ready.fetch_add(1, std::memory_order_relaxed);
        while (!start.load(std::memory_order_acquire)) {}
        while (!stop.load(std::memory_order_relaxed)) {
            if (queue.push(order)) order = makeOrder(++pushed);
            else ++full;
        }
        result.pushed = pushed;
        result.full = full;
    });
    std::thread t2([&] {
        std::uint64_t popped = 0, empty = 0;
        bool valid = true;
        Order order;
        ready.fetch_add(1, std::memory_order_relaxed);
        while (!start.load(std::memory_order_acquire)) {}
        while (!stop.load(std::memory_order_relaxed)) {
            if (queue.pop(order)) {
                valid = matches(order, popped) && valid;
                ++popped;
            } else ++empty;
        }
        result.popped = popped;
        result.empty = empty;
        result.valid = valid;
    });
    while (ready.load(std::memory_order_relaxed) != 2) std::this_thread::yield();
    const auto begin = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);
    std::this_thread::sleep_until(begin + std::chrono::seconds(1));
    stop.store(true, std::memory_order_relaxed);
    t1.join();
    t2.join();
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();

    // Verify leftovers after timing; they are not counted as timed pops.
    Order order;
    while (queue.pop(order)) {
        result.valid = matches(order, result.popped + result.remaining) && result.valid;
        ++result.remaining;
    }
    result.valid = result.valid && result.remaining <= 1024 &&
                   result.pushed == result.popped + result.remaining;
    return result;
}

void printResult(const char* name, int round, const Result& result) {
    const double rate = static_cast<double>(result.popped) / result.seconds;
    std::cout << name << ',' << round << ',' << result.pushed << ',' << result.popped
              << ',' << std::fixed << std::setprecision(6) << result.seconds
              << ',' << std::setprecision(3) << rate / 1e6
              << ',' << rate * sizeof(Order) / (1024.0 * 1024.0)
              << ',' << result.full << ',' << result.empty << ',' << result.remaining
              << ',' << (result.valid ? "PASS" : "FAIL") << '\n';
    require(result.valid, "Benchmark lost, duplicated or corrupted an object");
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--test") {
            testQueue<SpinLock>();
            testQueue<std::mutex>();
            std::cout << "PASS: empty/full, capacity one, wraparound, FIFO and payload; "
                         "100000 concurrent transfers per lock\n";
            return 0;
        }
        int rounds = 3;
        if (argc > 2) throw std::invalid_argument("Too many arguments");
        if (argc == 2) {
            std::size_t used = 0;
            rounds = std::stoi(argv[1], &used);
            if (used != std::string(argv[1]).size() || rounds < 1 || rounds > 100)
                throw std::invalid_argument("Rounds must be 1 through 100");
        }
        std::cout << "lock,round,pushed,popped,seconds,M_objects_per_sec,MiB_per_sec,"
                     "full_retries,empty_retries,remaining,validation\n";
        for (int round = 1; round <= rounds; ++round) {
            // Reverse the order each round to reduce consistent first-run bias.
            if (round % 2) {
                printResult("spinlock", round, benchmark<SpinLock>());
                printResult("std::mutex", round, benchmark<std::mutex>());
            } else {
                printResult("std::mutex", round, benchmark<std::mutex>());
                printResult("spinlock", round, benchmark<SpinLock>());
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\nUsage: ./spsc_queue [rounds | --test]\n";
        return 1;
    }
}
