// SPSC Queue Assignment - HFT C++
// Student Details:
// Email: angel.24bcs10011@sst.scaler.com
// Roll No: 10011

#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <vector>
#include <cstdint>
#include <iomanip>
#include <immintrin.h>

// 64-byte object as specified in the assignment
struct alignas(64) Object64 {
    uint64_t data[8]; // 8 * 8 = 64 bytes

    Object64() {
        for (int i = 0; i < 8; ++i) data[i] = 0;
    }
    explicit Object64(uint64_t val) {
        data[0] = val;
        for (int i = 1; i < 8; ++i) data[i] = val + i;
    }
};

static_assert(sizeof(Object64) == 64, "Object64 must be exactly 64 bytes");

// Global sink to prevent compiler dead-code elimination
std::atomic<uint64_t> g_sink{0};

// ============================================================================
// 1. Mutex-based SPSC Queue (Lec-10)
// ============================================================================
template <typename T, size_t Capacity = 65536>
class MutexSPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static constexpr size_t kMask = Capacity - 1;

    std::vector<T> buffer_;
    size_t head_{0};
    size_t tail_{0};
    std::mutex mtx_;

public:
    MutexSPSCQueue() : buffer_(Capacity) {}

    bool push(const T& item) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (tail_ - head_ == Capacity) {
            return false; // full
        }
        buffer_[tail_ & kMask] = item;
        ++tail_;
        return true;
    }

    bool pop(T& item) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (head_ == tail_) {
            return false; // empty
        }
        item = buffer_[head_ & kMask];
        ++head_;
        return true;
    }
};

// ============================================================================
// 2. Spinlock-based SPSC Queue (Lec-11: while loop atomic_flag)
// ============================================================================
struct Spinlock {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
            #if defined(__x86_64__) || defined(_M_X64)
            _mm_pause();
            #endif
        }
    }

    void unlock() {
        flag.clear(std::memory_order_release);
    }
};

template <typename T, size_t Capacity = 65536>
class SpinlockSPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static constexpr size_t kMask = Capacity - 1;

    std::vector<T> buffer_;
    size_t head_{0};
    size_t tail_{0};
    Spinlock lock_;

public:
    SpinlockSPSCQueue() : buffer_(Capacity) {}

    bool push(const T& item) {
        lock_.lock();
        if (tail_ - head_ == Capacity) {
            lock_.unlock();
            return false; // full
        }
        buffer_[tail_ & kMask] = item;
        ++tail_;
        lock_.unlock();
        return true;
    }

    bool pop(T& item) {
        lock_.lock();
        if (head_ == tail_) {
            lock_.unlock();
            return false; // empty
        }
        item = buffer_[head_ & kMask];
        ++head_;
        lock_.unlock();
        return true;
    }
};

// ============================================================================
// 3. Lock-free Atomic Ring Buffer (Lec-12: Separated cache lines to avoid false sharing)
// ============================================================================
template <typename T, size_t Capacity = 65536>
class LockFreeSPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static constexpr size_t kMask = Capacity - 1;

    alignas(64) std::atomic<size_t> tail_{0};
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::vector<T> buffer_;

public:
    LockFreeSPSCQueue() : buffer_(Capacity) {}

    bool push(const T& item) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        if (current_tail - head_.load(std::memory_order_acquire) == Capacity) {
            return false; // full
        }
        buffer_[current_tail & kMask] = item;
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false; // empty
        }
        item = buffer_[current_head & kMask];
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }
};

// ============================================================================
// Benchmark Runner for 1 Second
// ============================================================================
template <typename QueueType>
void run_1_second_benchmark(const std::string& name) {
    QueueType queue;
    std::atomic<bool> start_flag{false};
    std::atomic<bool> stop_flag{false};

    uint64_t push_count = 0;
    uint64_t pop_count = 0;

    std::thread producer([&]() {
        while (!start_flag.load(std::memory_order_acquire)) {
            #if defined(__x86_64__) || defined(_M_X64)
            _mm_pause();
            #endif
        }
        uint64_t seq = 0;
        while (!stop_flag.load(std::memory_order_relaxed)) {
            Object64 obj(seq);
            if (queue.push(obj)) {
                ++push_count;
                ++seq;
            }
        }
    });

    std::thread consumer([&]() {
        while (!start_flag.load(std::memory_order_acquire)) {
            #if defined(__x86_64__) || defined(_M_X64)
            _mm_pause();
            #endif
        }
        Object64 obj;
        uint64_t local_sink = 0;
        while (!stop_flag.load(std::memory_order_relaxed)) {
            if (queue.pop(obj)) {
                ++pop_count;
                local_sink += obj.data[0];
            }
        }
        g_sink.fetch_add(local_sink, std::memory_order_relaxed);
    });

    auto start_time = std::chrono::steady_clock::now();
    start_flag.store(true, std::memory_order_release);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    stop_flag.store(true, std::memory_order_release);
    auto end_time = std::chrono::steady_clock::now();

    producer.join();
    consumer.join();

    std::chrono::duration<double> elapsed = end_time - start_time;
    double actual_secs = elapsed.count();
    double pushes_per_sec = push_count / actual_secs;
    double pops_per_sec = pop_count / actual_secs;
    double mb_pushed_per_sec = (pushes_per_sec * sizeof(Object64)) / (1024.0 * 1024.0);
    double mb_popped_per_sec = (pops_per_sec * sizeof(Object64)) / (1024.0 * 1024.0);

    std::cout << "------------------------------------------------------------\n";
    std::cout << "Implementation: " << name << "\n";
    std::cout << "  Duration:          " << std::fixed << std::setprecision(4) << actual_secs << " seconds\n";
    std::cout << "  64B Objects Pushed: " << push_count << " (" << std::fixed << std::setprecision(2)
              << pushes_per_sec / 1e6 << " Mops/s | " << mb_pushed_per_sec << " MB/s)\n";
    std::cout << "  64B Objects Popped: " << pop_count << " (" << std::fixed << std::setprecision(2)
              << pops_per_sec / 1e6 << " Mops/s | " << mb_popped_per_sec << " MB/s)\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << " HFT C++ SPSC Queue 1-Second Benchmark (64-byte Objects)\n";
    std::cout << " Student: Angel (Roll No: 10011)\n";
    std::cout << " Email: angel.24bcs10011@sst.scaler.com\n";
    std::cout << "============================================================\n\n";

    run_1_second_benchmark<MutexSPSCQueue<Object64>>("1. std::mutex SPSC Queue");
    run_1_second_benchmark<SpinlockSPSCQueue<Object64>>("2. Atomic Spinlock SPSC Queue");
    run_1_second_benchmark<LockFreeSPSCQueue<Object64>>("3. Lock-free Atomic SPSC Queue (alignas 64)");

    std::cout << "\n============================================================\n";
    std::cout << "Sink verification: " << g_sink.load() << "\n";
    std::cout << "============================================================\n";

    return 0;
}
