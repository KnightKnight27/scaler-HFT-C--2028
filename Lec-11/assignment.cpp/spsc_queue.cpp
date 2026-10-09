#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <memory>
#include <vector>

// ============================================================================
// 1. 64-BYTE PAYLOAD STRUCT
// ============================================================================
// Aligned to 64 bytes so that each message exactly fills one cache line.
// Prevents false sharing and split-cache-line memory access penalties.
struct alignas(64) Message {
    uint64_t seq;
    uint64_t timestamp;
    char payload[48];
};

static_assert(sizeof(Message) == 64, "Message size must be exactly 64 bytes");
static_assert(alignof(Message) == 64, "Message alignment must be 64 bytes");

// Helper macro/function for x86 CPU pause instruction
inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
    __asm__ __volatile__("yield" ::: "memory");
#endif
}

// ============================================================================
// 2. SPINLOCK GUARDED RING BUFFER QUEUE (LockedQueue)
// ============================================================================
// Uses std::atomic_flag with test-and-set and CPU pause instruction.
class Spinlock {
public:
    void lock() noexcept {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            cpu_relax();
        }
    }

    void unlock() noexcept {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

template <typename T, size_t Capacity = 65536>
class LockedQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static constexpr size_t IndexMask = Capacity - 1;

public:
    LockedQueue() : buffer_(new T[Capacity]) {}

    ~LockedQueue() {
        delete[] buffer_;
    }

    LockedQueue(const LockedQueue&) = delete;
    LockedQueue& operator=(const LockedQueue&) = delete;

    bool push(const T& val) {
        lock_.lock();
        if (tail_ - head_ >= Capacity) {
            lock_.unlock();
            return false; // Queue full
        }
        buffer_[tail_ & IndexMask] = val;
        ++tail_;
        lock_.unlock();
        return true;
    }

    bool pop(T& val) {
        lock_.lock();
        if (head_ == tail_) {
            lock_.unlock();
            return false; // Queue empty
        }
        val = buffer_[head_ & IndexMask];
        ++head_;
        lock_.unlock();
        return true;
    }

    size_t size() const {
        return tail_ - head_;
    }

private:
    Spinlock lock_;
    T* buffer_;
    size_t head_{0};
    size_t tail_{0};
};

// ============================================================================
// 3. LOCK-FREE CACHE-ALIGNED SPSC QUEUE (SPSCQueue)
// ============================================================================
// Ultra-low-latency Single-Producer Single-Consumer lock-free circular queue.
// - Power-of-two buffer with bitwise AND masking instead of expensive modulo (%).
// - Isolated head and tail atomics using alignas(64) on distinct cache lines
//   to eliminate false sharing between producer and consumer cores.
// - Local shadow copies (cached_head, cached_tail) to avoid cross-core bus
//   synchronization on every single push/pop cycle.
// - Explicit memory orders: acquire, release, relaxed.
template <typename T, size_t Capacity = 65536>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static constexpr size_t IndexMask = Capacity - 1;
    static constexpr size_t CacheLineSize = 64;

public:
    SPSCQueue() : buffer_(new T[Capacity]) {}

    ~SPSCQueue() {
        delete[] buffer_;
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // Called exclusively by producer thread
    bool push(const T& val) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);

        // Fast path: check against local cached head to prevent cross-core cache invalidation
        if (current_tail - cached_head_ >= Capacity) {
            // Slow path: reload remote head atomic with acquire semantics
            cached_head_ = head_.load(std::memory_order_acquire);
            if (current_tail - cached_head_ >= Capacity) {
                return false; // Queue is full
            }
        }

        buffer_[current_tail & IndexMask] = val;

        // Release order: ensures payload write is globally visible prior to advancing tail
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    // Called exclusively by consumer thread
    bool pop(T& val) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        // Fast path: check against local cached tail
        if (current_head == cached_tail_) {
            // Slow path: reload remote tail atomic with acquire semantics
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (current_head == cached_tail_) {
                return false; // Queue is empty
            }
        }

        val = buffer_[current_head & IndexMask];

        // Release order: ensures payload read finishes prior to notifying producer that slot is free
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

private:
    // Buffer pointer
    alignas(CacheLineSize) T* buffer_;

    // ================= Producer Cache Line =================
    // Only producer writes tail_ and reads cached_head_
    alignas(CacheLineSize) std::atomic<size_t> tail_{0};
    size_t cached_head_{0};

    // ================= Consumer Cache Line =================
    // Only consumer writes head_ and reads cached_tail_
    alignas(CacheLineSize) std::atomic<size_t> head_{0};
    size_t cached_tail_{0};

    // Padding to safeguard adjacent heap/stack structures from false sharing
    char padding_[CacheLineSize - sizeof(std::atomic<size_t>) - sizeof(size_t)];
};

// ============================================================================
// 4. MULTI-THREADED BENCHMARK HARNESS
// ============================================================================
struct BenchmarkResult {
    std::string queue_name;
    uint64_t push_count;
    uint64_t pop_count;
    double duration_seconds;

    double push_ops_per_sec() const {
        return static_cast<double>(push_count) / duration_seconds;
    }

    double pop_ops_per_sec() const {
        return static_cast<double>(pop_count) / duration_seconds;
    }
};

template <typename QueueType>
BenchmarkResult run_benchmark(const std::string& name, std::chrono::milliseconds duration = std::chrono::milliseconds(1000)) {
    QueueType queue;

    std::atomic<bool> start_signal{false};
    std::atomic<bool> stop_signal{false};
    uint64_t pushed_ops = 0;
    uint64_t popped_ops = 0;

    // Producer thread
    std::thread producer([&]() {
        // Spin-wait for start signal
        while (!start_signal.load(std::memory_order_acquire)) {
            cpu_relax();
        }

        Message msg;
        msg.seq = 0;
        msg.timestamp = 1000;
        std::memset(msg.payload, 'A', sizeof(msg.payload));

        while (!stop_signal.load(std::memory_order_relaxed)) {
            msg.seq = pushed_ops + 1;
            if (queue.push(msg)) {
                ++pushed_ops;
            } else {
                cpu_relax();
            }
        }
    });

    // Consumer thread
    std::thread consumer([&]() {
        // Spin-wait for start signal
        while (!start_signal.load(std::memory_order_acquire)) {
            cpu_relax();
        }

        Message msg;
        while (!stop_signal.load(std::memory_order_relaxed)) {
            if (queue.pop(msg)) {
                ++popped_ops;
            } else {
                cpu_relax();
            }
        }
    });

    // Allow threads to initialize and reach the spin-wait state
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Start benchmark window
    const auto start_time = std::chrono::high_resolution_clock::now();
    start_signal.store(true, std::memory_order_release);

    // Run for timed window (exactly 1 second)
    std::this_thread::sleep_for(duration);

    // Stop benchmark
    stop_signal.store(true, std::memory_order_release);
    const auto end_time = std::chrono::high_resolution_clock::now();

    producer.join();
    consumer.join();

    const std::chrono::duration<double> elapsed = end_time - start_time;

    return BenchmarkResult{
        name,
        pushed_ops,
        popped_ops,
        elapsed.count()
    };
}

void print_result(const BenchmarkResult& res) {
    std::cout << "------------------------------------------------------------\n";
    std::cout << " Queue Architecture: " << res.queue_name << "\n";
    std::cout << "------------------------------------------------------------\n";
    std::cout << " Elapsed Duration   : " << std::fixed << std::setprecision(4) << res.duration_seconds << " s\n";
    std::cout << " Messages Pushed    : " << res.push_count << " ops\n";
    std::cout << " Messages Popped    : " << res.pop_count << " ops\n";
    std::cout << " Push Throughput    : " << std::fixed << std::setprecision(2)
              << (res.push_ops_per_sec() / 1e6) << " Million ops/sec\n";
    std::cout << " Pop Throughput     : " << std::fixed << std::setprecision(2)
              << (res.pop_ops_per_sec() / 1e6) << " Million ops/sec\n";
    std::cout << " Total Throughput   : " << std::fixed << std::setprecision(2)
              << ((res.push_ops_per_sec() + res.pop_ops_per_sec()) / 1e6) << " Million ops/sec\n";
    std::cout << "------------------------------------------------------------\n\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << " HFT SPSC QUEUE BENCHMARK HARNESS (64-byte payload, 1.0s)   \n";
    std::cout << "============================================================\n";
    std::cout << " Payload Size       : " << sizeof(Message) << " bytes\n";
    std::cout << " Payload Alignment  : " << alignof(Message) << " bytes\n";
    std::cout << " Ring Capacity      : 65536 slots (Power of 2)\n\n";

    std::cout << "[*] Running Spinlock Queue (LockedQueue) Benchmark for 1.0s...\n";
    auto locked_res = run_benchmark<LockedQueue<Message>>("LockedQueue (std::atomic_flag + pause)");
    print_result(locked_res);

    std::cout << "[*] Running Lock-Free SPSC Queue (SPSCQueue) Benchmark for 1.0s...\n";
    auto spsc_res = run_benchmark<SPSCQueue<Message>>("SPSCQueue (Lock-Free Cache-Aligned)");
    print_result(spsc_res);

    double speedup = spsc_res.pop_ops_per_sec() / locked_res.pop_ops_per_sec();
    std::cout << "============================================================\n";
    std::cout << " SUMMARY & PERFORMANCE COMPARISON:\n";
    std::cout << " Lock-Free SPSC Pop Throughput Speedup: " << std::fixed << std::setprecision(2)
              << speedup << "x faster than Spinlock Queue\n";
    std::cout << "============================================================\n";

    return 0;
}
