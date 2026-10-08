// WRITE AN SPSC QUEUE 
// SPINLOCK ( WHILE LOOP) OR STD::MUTEX 
// t1.join()  t2.join()
// producer consumer to push objects and pop objects 
//
// you need to figure out a way that with locks how many 
// 64 byte objects can u push and pop in 1 second
//  raise a git PR for the same 
//  add readme for ur per second specs 
//  feel free to add worst code qaulity :)
//
//
// ^^ MEMORY POOL ^^

#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <memory>
#include <cassert>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define CPU_PAUSE() _mm_pause()
#elif defined(__aarch64__) || defined(_M_ARM64)
#define CPU_PAUSE() asm volatile("yield" ::: "memory")
#else
#define CPU_PAUSE() do {} while (0)
#endif

// -----------------------------------------------------------------------------
// 64-byte payload struct representing an HFT order / market-data event
// -----------------------------------------------------------------------------
struct alignas(64) Message {
    uint64_t seq_num;
    uint64_t timestamp_ns;
    char payload[48];
};
static_assert(sizeof(Message) == 64, "Message struct must be exactly 64 bytes");

// -----------------------------------------------------------------------------
// TTAS (Test-and-Test-and-Set) SpinLock with architecture-specific CPU pause
// -----------------------------------------------------------------------------
class SpinLock {
    std::atomic<bool> locked_{false};

public:
    void lock() noexcept {
        while (locked_.exchange(true, std::memory_order_acquire)) {
            while (locked_.load(std::memory_order_relaxed)) {
                CPU_PAUSE();
            }
        }
    }

    void unlock() noexcept {
        locked_.store(false, std::memory_order_release);
    }
};

// -----------------------------------------------------------------------------
// SPSC Ring Buffer Queue using SpinLock (While-loop lock)
// Ring buffer memory pool pre-allocates all slots (zero heap allocation in hot path)
// -----------------------------------------------------------------------------
template <size_t Capacity = 65536>
class SPSCSpinlockQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr size_t kMask = Capacity - 1;

    alignas(64) Message buffer_[Capacity];
    alignas(64) size_t head_{0}; // Consumer read index
    alignas(64) size_t tail_{0}; // Producer write index
    alignas(64) SpinLock lock_;

public:
    static constexpr size_t capacity() { return Capacity; }

    bool push(const Message& msg) {
        lock_.lock();
        if (tail_ - head_ >= Capacity) {
            lock_.unlock();
            return false; // Queue full
        }
        buffer_[tail_ & kMask] = msg;
        ++tail_;
        lock_.unlock();
        return true;
    }

    bool pop(Message& msg) {
        lock_.lock();
        if (head_ == tail_) {
            lock_.unlock();
            return false; // Queue empty
        }
        msg = buffer_[head_ & kMask];
        ++head_;
        lock_.unlock();
        return true;
    }

    size_t size() const {
        return tail_ - head_;
    }
};

// -----------------------------------------------------------------------------
// SPSC Ring Buffer Queue using std::mutex
// Ring buffer memory pool pre-allocates all slots (zero heap allocation in hot path)
// -----------------------------------------------------------------------------
template <size_t Capacity = 65536>
class SPSCMutexQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr size_t kMask = Capacity - 1;

    alignas(64) Message buffer_[Capacity];
    alignas(64) size_t head_{0}; // Consumer read index
    alignas(64) size_t tail_{0}; // Producer write index
    alignas(64) std::mutex mutex_;

public:
    static constexpr size_t capacity() { return Capacity; }

    bool push(const Message& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tail_ - head_ >= Capacity) {
            return false; // Queue full
        }
        buffer_[tail_ & kMask] = msg;
        ++tail_;
        return true;
    }

    bool pop(Message& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (head_ == tail_) {
            return false; // Queue empty
        }
        msg = buffer_[head_ & kMask];
        ++head_;
        return true;
    }

    size_t size() const {
        return tail_ - head_;
    }
};

// -----------------------------------------------------------------------------
// Bonus / Reference: Lock-Free SPSC Ring Buffer Queue (Acquire-Release Atomics)
// Included to showcase the latency/throughput baseline against locked queues
// -----------------------------------------------------------------------------
template <size_t Capacity = 65536>
class SPSCLockFreeQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr size_t kMask = Capacity - 1;

    alignas(64) Message buffer_[Capacity];
    alignas(64) std::atomic<size_t> tail_{0}; // Written by producer
    alignas(64) std::atomic<size_t> head_{0}; // Written by consumer

public:
    static constexpr size_t capacity() { return Capacity; }

    bool push(const Message& msg) {
        const size_t t = tail_.load(std::memory_order_relaxed);
        const size_t h = head_.load(std::memory_order_acquire);
        if (t - h >= Capacity) {
            return false;
        }
        buffer_[t & kMask] = msg;
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    bool pop(Message& msg) {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t t = tail_.load(std::memory_order_acquire);
        if (h == t) {
            return false;
        }
        msg = buffer_[h & kMask];
        head_.store(h + 1, std::memory_order_release);
        return true;
    }
};

// -----------------------------------------------------------------------------
// Correctness Test Harness
// -----------------------------------------------------------------------------
template <typename QueueType>
void test_correctness(const std::string& name) {
    auto q = std::make_unique<QueueType>();
    Message dummy{};

    // 1. Verify pop on empty queue fails
    assert(!q->pop(dummy) && "Empty queue pop should fail");

    // 2. Fill queue to capacity and verify it rejects further pushes
    for (size_t i = 0; i < QueueType::capacity(); ++i) {
        Message m{i, 0, {}};
        assert(q->push(m) && "Push within capacity should succeed");
    }
    assert(!q->push(dummy) && "Push to full queue should fail");

    // 3. Drain queue and verify FIFO order
    for (size_t i = 0; i < QueueType::capacity(); ++i) {
        Message m{};
        assert(q->pop(m) && "Pop from non-empty queue should succeed");
        assert(m.seq_num == i && "FIFO order preserved");
    }
    assert(!q->pop(dummy) && "Queue should now be empty");

    // 4. Concurrent producer-consumer test (1,000,000 messages)
    constexpr uint64_t kTestTotal = 1'000'000;
    std::atomic<bool> failed{false};

    std::thread t1([&]() {
        for (uint64_t i = 0; i < kTestTotal; ++i) {
            Message m{i, i * 10, {}};
            m.payload[0] = static_cast<char>(i & 0xFF);
            while (!q->push(m)) {
                CPU_PAUSE();
            }
        }
    });

    std::thread t2([&]() {
        for (uint64_t i = 0; i < kTestTotal; ++i) {
            Message m{};
            while (!q->pop(m)) {
                CPU_PAUSE();
            }
            if (m.seq_num != i || m.payload[0] != static_cast<char>(i & 0xFF)) {
                failed.store(true);
            }
        }
    });

    t1.join();
    t2.join();

    assert(!failed.load() && "Concurrent FIFO and data integrity check failed");
    std::cout << "  [PASS] " << name << " correctness verified (FIFO, boundaries, concurrency).\n";
}

// -----------------------------------------------------------------------------
// 1.0-Second Throughput Benchmark
// -----------------------------------------------------------------------------
struct BenchmarkResult {
    std::string name;
    uint64_t pushed{0};
    uint64_t popped{0};
    double duration_seconds{0.0};
    double push_ops_per_sec{0.0};
    double pop_ops_per_sec{0.0};
    double throughput_mb_per_sec{0.0};
};

template <typename QueueType>
BenchmarkResult run_benchmark(const std::string& name, double duration_target_sec = 1.0) {
    auto queue = std::make_unique<QueueType>();

    std::atomic<bool> ready{false};
    std::atomic<bool> stop{false};
    uint64_t pushed_count = 0;
    uint64_t popped_count = 0;

    // Producer thread: pushes 64-byte objects
    std::thread t1([&]() {
        while (!ready.load(std::memory_order_acquire)) {
            CPU_PAUSE();
        }

        Message msg{};
        msg.timestamp_ns = 1000;
        std::memset(msg.payload, 'X', sizeof(msg.payload));

        while (!stop.load(std::memory_order_relaxed)) {
            msg.seq_num = pushed_count;
            if (queue->push(msg)) {
                ++pushed_count;
            }
        }
    });

    // Consumer thread: pops 64-byte objects
    std::thread t2([&]() {
        while (!ready.load(std::memory_order_acquire)) {
            CPU_PAUSE();
        }

        Message msg{};
        while (!stop.load(std::memory_order_relaxed)) {
            if (queue->pop(msg)) {
                ++popped_count;
            }
        }
    });

    // Short stabilization delay before measurement window
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Start benchmark window
    auto start_time = std::chrono::high_resolution_clock::now();
    ready.store(true, std::memory_order_release);

    // Run for target duration (1.0 second)
    std::this_thread::sleep_for(std::chrono::duration<double>(duration_target_sec));
    stop.store(true, std::memory_order_release);

    auto end_time = std::chrono::high_resolution_clock::now();

    // Join both threads as instructed: t1.join(), t2.join()
    t1.join();
    t2.join();

    std::chrono::duration<double> elapsed = end_time - start_time;
    double actual_sec = elapsed.count();

    BenchmarkResult res;
    res.name = name;
    res.pushed = pushed_count;
    res.popped = popped_count;
    res.duration_seconds = actual_sec;
    res.push_ops_per_sec = static_cast<double>(pushed_count) / actual_sec;
    res.pop_ops_per_sec = static_cast<double>(popped_count) / actual_sec;
    res.throughput_mb_per_sec = (static_cast<double>(popped_count) * sizeof(Message)) / (1024.0 * 1024.0 * actual_sec);

    return res;
}

void print_result(const BenchmarkResult& res) {
    std::cout << "---------------------------------------------------------\n";
    std::cout << "  Queue Type: " << res.name << "\n";
    std::cout << "---------------------------------------------------------\n";
    std::cout << "  Window Duration:    " << std::fixed << std::setprecision(4) << res.duration_seconds << " s\n";
    std::cout << "  Objects Pushed:     " << res.pushed << " ops\n";
    std::cout << "  Objects Popped:     " << res.popped << " ops\n";
    std::cout << "  Push Throughput:    " << std::fixed << std::setprecision(2) << (res.push_ops_per_sec / 1e6) << " M ops/sec\n";
    std::cout << "  Pop Throughput:     " << std::fixed << std::setprecision(2) << (res.pop_ops_per_sec / 1e6) << " M ops/sec\n";
    std::cout << "  Data Bandwidth:     " << std::fixed << std::setprecision(2) << res.throughput_mb_per_sec << " MB/s (" 
              << (res.throughput_mb_per_sec / 1024.0) << " GB/s)\n";
}

int main() {
    std::cout << "=========================================================\n";
    std::cout << " HFT C++ SPSC Queue Assignment: 1-Second Benchmark      \n";
    std::cout << " Message Size: " << sizeof(Message) << " bytes (Aligned to 64 bytes) \n";
    std::cout << " Queue Capacity: 65,536 elements (Pre-allocated pool)   \n";
    std::cout << "=========================================================\n\n";

    std::cout << "Phase 1: Correctness Verification\n";
    test_correctness<SPSCSpinlockQueue<65536>>("SPSC SpinLock Queue");
    test_correctness<SPSCMutexQueue<65536>>("SPSC std::mutex Queue");
    test_correctness<SPSCLockFreeQueue<65536>>("SPSC Lock-Free Queue (Reference)");
    std::cout << "\nAll correctness tests passed successfully!\n\n";

    std::cout << "Phase 2: 1.0-Second High-Performance Benchmarking\n";

    std::cout << "\nRunning SPSC SpinLock Queue benchmark...\n";
    auto spinlock_res = run_benchmark<SPSCSpinlockQueue<65536>>("SpinLock SPSC Queue (while loop)");
    print_result(spinlock_res);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "\nRunning SPSC std::mutex Queue benchmark...\n";
    auto mutex_res = run_benchmark<SPSCMutexQueue<65536>>("std::mutex SPSC Queue");
    print_result(mutex_res);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "\nRunning SPSC Lock-Free Queue (Reference)...\n";
    auto lockfree_res = run_benchmark<SPSCLockFreeQueue<65536>>("Lock-Free SPSC Queue (Atomics)");
    print_result(lockfree_res);

    std::cout << "\n=========================================================\n";
    std::cout << " Benchmark Summary (1-Second Window):\n";
    std::cout << "=========================================================\n";
    std::cout << std::left << std::setw(34) << "Queue Type" 
              << std::right << std::setw(14) << "Popped Ops" 
              << std::setw(22) << "Throughput (ops/s)" 
              << std::setw(18) << "Bandwidth (MB/s)" << "\n";
    std::cout << "------------------------------------------------------------------------------------------------\n";
    std::cout << std::left << std::setw(34) << spinlock_res.name 
              << std::right << std::setw(14) << spinlock_res.popped 
              << std::setw(22) << static_cast<uint64_t>(spinlock_res.pop_ops_per_sec) 
              << std::setw(18) << std::fixed << std::setprecision(2) << spinlock_res.throughput_mb_per_sec << "\n";
    std::cout << std::left << std::setw(34) << mutex_res.name 
              << std::right << std::setw(14) << mutex_res.popped 
              << std::setw(22) << static_cast<uint64_t>(mutex_res.pop_ops_per_sec) 
              << std::setw(18) << std::fixed << std::setprecision(2) << mutex_res.throughput_mb_per_sec << "\n";
    std::cout << std::left << std::setw(34) << lockfree_res.name 
              << std::right << std::setw(14) << lockfree_res.popped 
              << std::setw(22) << static_cast<uint64_t>(lockfree_res.pop_ops_per_sec) 
              << std::setw(18) << std::fixed << std::setprecision(2) << lockfree_res.throughput_mb_per_sec << "\n";
    std::cout << "================================================================================================\n";

    return 0;
}
