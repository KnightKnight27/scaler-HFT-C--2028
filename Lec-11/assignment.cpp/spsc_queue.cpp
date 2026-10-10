// Lec-11 Assignment: SPSC Queue benchmark
// Name: Tanishka Mangure — Roll: 24bcs10264
//
// Single-producer / single-consumer ring buffer holding 64-byte objects.
// Backed by a preallocated memory pool (no allocation in the hot path).
// Compares std::mutex vs spinlock (atomic_flag while-loop) over a 1-second window.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

// 64-byte payload: exactly one cache line on x86-64, typical for HFT feeds.
struct Payload64 {
    char data[64];
};
static_assert(sizeof(Payload64) == 64, "Payload64 must be 64 bytes");

// Simple spinlock: busy while-loop on atomic_flag.
class Spinlock {
public:
    void lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // Single yield point keeps single-core runs from wedging;
            // remove for a pure spin on dedicated cores.
            // std::this_thread::yield();
        }
    }
    void unlock() { flag_.clear(std::memory_order_release); }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

// Fixed-size ring buffer. Capacity is rounded up to a power of two so
// indexing uses a bitmask. The pool vector is the memory pool: allocated
// once in the constructor, then reused via copy in push/pop.
template <typename Lock>
class SpscQueue {
public:
    explicit SpscQueue(std::size_t capacity = 8192) {
        std::size_t p = 1;
        while (p < capacity) {
            p <<= 1;
        }
        capacity_ = p;
        mask_ = p - 1;
        pool_.resize(capacity_);
    }

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    bool push(const Payload64& obj) {
        std::lock_guard<Lock> guard(mutex_);
        if (size_ == capacity_) {
            return false;  // full: drop, producer retries
        }
        pool_[tail_ & mask_] = obj;
        ++tail_;
        ++size_;
        return true;
    }

    bool pop(Payload64& out) {
        std::lock_guard<Lock> guard(mutex_);
        if (size_ == 0) {
            return false;  // empty: consumer retries
        }
        out = pool_[head_ & mask_];
        ++head_;
        --size_;
        return true;
    }

private:
    std::vector<Payload64> pool_;
    std::size_t capacity_ = 0;
    std::size_t mask_ = 0;
    std::size_t head_ = 0;  // consumer index
    std::size_t tail_ = 0;  // producer index
    std::size_t size_ = 0;
    Lock mutex_;
};

// Runs producer + consumer for exactly 1 second, returns popped count.
template <typename Lock>
uint64_t BenchmarkOneSecond(const char* label) {
    SpscQueue<Lock> queue(8192);
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> pushed{0};
    std::atomic<uint64_t> popped{0};

    std::thread t1([&] {  // producer
        Payload64 obj;
        std::memset(obj.data, 0xCD, sizeof(obj.data));
        uint64_t count = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            if (queue.push(obj)) {
                ++count;
            }
        }
        pushed.store(count, std::memory_order_relaxed);
    });

    std::thread t2([&] {  // consumer
        Payload64 obj;
        uint64_t count = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            if (queue.pop(obj)) {
                ++count;
            }
        }
        popped.store(count, std::memory_order_relaxed);
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop.store(true, std::memory_order_relaxed);
    t1.join();
    t2.join();

    const uint64_t p = pushed.load();
    const uint64_t c = popped.load();
    std::printf("%-10s pushed=%llu popped=%llu (64B objs, 1s)\n", label,
                static_cast<unsigned long long>(p),
                static_cast<unsigned long long>(c));
    return c;
}

int main() {
    std::printf("SPSC 64B throughput — cap 8192, memory-pool ring, 1s window\n");
    const uint64_t with_mutex = BenchmarkOneSecond<std::mutex>("mutex:");
    const uint64_t with_spin = BenchmarkOneSecond<Spinlock>("spinlock:");
    std::printf("summary: mutex=%llu/s spinlock=%llu/s winner=%s\n",
                static_cast<unsigned long long>(with_mutex),
                static_cast<unsigned long long>(with_spin),
                with_spin > with_mutex ? "spinlock" : "mutex");
    return 0;
}
