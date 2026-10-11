#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Exactly one cache line of payload per object.
struct alignas(64) Object {
    std::uint64_t sequence = 0;
    char payload[56]{};
};
static_assert(sizeof(Object) == 64, "Object must be exactly 64 bytes");

// A small BasicLockable wrapper so the queue can use either lock type.
class QueueLock {
public:
    explicit QueueLock(bool spinlock) : spinlock_(spinlock) {}

    void lock() {
        if (!spinlock_) {
            mutex_.lock();
            return;
        }
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // Deliberately simple busy-wait spinlock for comparison.
        }
    }

    void unlock() {
        if (!spinlock_) {
            mutex_.unlock();
            return;
        }
        flag_.clear(std::memory_order_release);
    }

private:
    bool spinlock_;
    std::mutex mutex_;
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

// The vector is allocated once in the constructor. Its slots are reused as a
// fixed-size memory pool; push/pop do not allocate memory in the hot path.
class SpscQueue {
public:
    SpscQueue(std::size_t capacity, bool use_spinlock)
        : slots_(capacity), lock_(use_spinlock) {
        if (capacity == 0) {
            throw std::invalid_argument("Queue capacity must be greater than zero");
        }
    }

    bool push(const Object& item) {
        std::lock_guard<QueueLock> guard(lock_);
        if (size_ == slots_.size()) {
            return false; // Queue full.
        }

        slots_[tail_] = item;
        tail_ = (tail_ + 1) % slots_.size();
        ++size_;
        return true;
    }

    bool pop(Object& item) {
        std::lock_guard<QueueLock> guard(lock_);
        if (size_ == 0) {
            return false; // Queue empty.
        }

        item = slots_[head_];
        head_ = (head_ + 1) % slots_.size();
        --size_;
        return true;
    }

private:
    std::vector<Object> slots_; // Preallocated reusable object pool.
    QueueLock lock_;
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t size_ = 0;
};

struct ProducerStats {
    std::uint64_t pushed = 0;
    std::uint64_t full = 0;
};

struct ConsumerStats {
    std::uint64_t popped = 0;
    std::uint64_t empty = 0;
    bool order_ok = true;
};

struct BenchmarkResult {
    ProducerStats producer;
    ConsumerStats consumer;
    double seconds = 0.0;
};

BenchmarkResult benchmark(std::size_t capacity, bool use_spinlock,
                          std::chrono::milliseconds duration) {
    SpscQueue queue(capacity, use_spinlock);
    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<unsigned int> ready{0};

    ProducerStats producer_stats;
    ConsumerStats consumer_stats;

    std::thread producer([&] {
        Object item{};
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            item.sequence = producer_stats.pushed;
            if (queue.push(item)) {
                ++producer_stats.pushed;
            } else {
                ++producer_stats.full;
            }
        }
    });

    std::thread consumer([&] {
        Object item{};
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            if (queue.pop(item)) {
                if (item.sequence != consumer_stats.popped) {
                    consumer_stats.order_ok = false;
                }
                ++consumer_stats.popped;
            } else {
                ++consumer_stats.empty;
            }
        }
    });

    while (ready.load(std::memory_order_acquire) != 2) {
        std::this_thread::yield();
    }

    const auto begin = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);
    std::this_thread::sleep_until(begin + duration);
    stop.store(true, std::memory_order_relaxed);
    const auto end = std::chrono::steady_clock::now();

    producer.join();
    consumer.join();

    BenchmarkResult result;
    result.producer = producer_stats;
    result.consumer = consumer_stats;
    result.seconds = std::chrono::duration<double>(end - begin).count();
    return result;
}

void print_result(const char* name, const BenchmarkResult& result) {
    const double push_rate = result.producer.pushed / result.seconds;
    const double pop_rate = result.consumer.popped / result.seconds;

    std::cout << "\n" << name << "\n"
              << "  elapsed seconds : " << std::fixed << std::setprecision(3)
              << result.seconds << "\n"
              << "  pushed          : " << result.producer.pushed
              << " objects (" << std::setprecision(0) << push_rate << "/sec)\n"
              << "  popped          : " << result.consumer.popped
              << " objects (" << pop_rate << "/sec)\n"
              << "  push failures   : " << result.producer.full << " (queue full)\n"
              << "  pop failures    : " << result.consumer.empty << " (queue empty)\n"
              << "  FIFO order      : "
              << (result.consumer.order_ok ? "OK" : "FAILED") << "\n";
}

int main(int argc, char** argv) {
    std::size_t capacity = 1024;
    std::uint64_t duration_ms = 1000;

    try {
        if (argc >= 2) {
            char* end = nullptr;
            const auto parsed = std::strtoull(argv[1], &end, 10);
            if (end == argv[1] || *end != '\0' || parsed == 0) {
                std::cerr << "Usage: " << argv[0]
                          << " [capacity>0] [duration_ms>0]\n";
                return 2;
            }
            capacity = static_cast<std::size_t>(parsed);
        }
        if (argc >= 3) {
            char* end = nullptr;
            duration_ms = std::strtoull(argv[2], &end, 10);
            if (end == argv[2] || *end != '\0' || duration_ms == 0) {
                std::cerr << "Usage: " << argv[0]
                          << " [capacity>0] [duration_ms>0]\n";
                return 2;
            }
        }
        if (argc > 3) {
            std::cerr << "Usage: " << argv[0]
                      << " [capacity>0] [duration_ms>0]\n";
            return 2;
        }

        const auto duration = std::chrono::milliseconds(duration_ms);
        std::cout << "SPSC queue benchmark (64-byte objects)\n"
                  << "capacity: " << capacity << ", duration: "
                  << duration_ms << " ms\n";

        const BenchmarkResult mutex_result = benchmark(capacity, false, duration);
        const BenchmarkResult spin_result = benchmark(capacity, true, duration);

        print_result("std::mutex", mutex_result);
        print_result("spinlock", spin_result);

        if (!mutex_result.consumer.order_ok || !spin_result.consumer.order_ok) {
            std::cerr << "\nERROR: FIFO sequence check failed.\n";
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 2;
    }
}
