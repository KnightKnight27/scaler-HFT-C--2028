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
#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstring>
#include <iomanip>
#include <cassert>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define CPU_PAUSE() _mm_pause()
#elif defined(__aarch64__) || defined(__arm64__)
#define CPU_PAUSE() asm volatile("yield")
#else
#define CPU_PAUSE() do {} while(0)
#endif

// -------------------------------------------------------------
// 1. 64-Byte Object aligned to standard CPU cache line
// -------------------------------------------------------------
struct alignas(64) Object {
    uint64_t sequence{0};
    uint64_t timestamp{0};
    uint8_t  payload[48]{0};

    Object() = default;
    explicit Object(uint64_t seq, uint64_t ts = 0)
        : sequence(seq), timestamp(ts) {
        std::memset(payload, static_cast<int>(seq & 0xFF), sizeof(payload));
    }
};

static_assert(sizeof(Object) == 64, "Object must be exactly 64 bytes");
static_assert(alignof(Object) == 64, "Object must be 64-byte cache-line aligned");

// -------------------------------------------------------------
// 2. SpinLock (While Loop with atomic test-and-set and CPU yield)
// -------------------------------------------------------------
class SpinLock {
private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

public:
    SpinLock() = default;
    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

    void lock() noexcept {
        while (flag.test_and_set(std::memory_order_acquire)) {
            CPU_PAUSE(); // Busy-wait with CPU yield hint
        }
    }

    void unlock() noexcept {
        flag.clear(std::memory_order_release);
    }
};

// -------------------------------------------------------------
// 3. SPSC Queue with Memory Pool Architecture
// The queue preallocates contiguous slots at construction time.
// This acts as a preallocated memory pool, eliminating malloc/free
// heap contention during the critical push/pop loop.
// -------------------------------------------------------------
template <typename T, typename LockPolicy>
class SPSCQueue {
private:
    std::vector<T> pool_; // Preallocated memory pool buffer
    const size_t capacity_;
    const size_t mask_;
    alignas(64) size_t head_{0};  // Read index (consumer)
    alignas(64) size_t tail_{0};  // Write index (producer)
    alignas(64) size_t count_{0}; // Elements currently in queue
    alignas(64) LockPolicy lock_; // Custom SpinLock or std::mutex

public:
    explicit SPSCQueue(size_t capacity)
        : pool_(capacity), capacity_(capacity), mask_(capacity - 1) {
        // Capacity must be power of 2 for fast bitwise masking
        assert((capacity & (capacity - 1)) == 0 && "Capacity must be power of 2");
    }

    bool push(const T& item) noexcept {
        std::lock_guard<LockPolicy> guard(lock_);
        if (count_ == capacity_) {
            return false; // Queue full
        }
        pool_[tail_ & mask_] = item;
        ++tail_;
        ++count_;
        return true;
    }

    bool pop(T& item) noexcept {
        std::lock_guard<LockPolicy> guard(lock_);
        if (count_ == 0) {
            return false; // Queue empty
        }
        item = pool_[head_ & mask_];
        ++head_;
        --count_;
        return true;
    }

    size_t size() const noexcept {
        return count_;
    }

    size_t capacity() const noexcept {
        return capacity_;
    }
};

// -------------------------------------------------------------
// 4. Benchmark Runner: 1-Second Timed Push & Pop
// -------------------------------------------------------------
template <typename LockPolicy>
void run_benchmark(const std::string& lock_name, size_t capacity, double duration_sec = 1.0) {
    SPSCQueue<Object, LockPolicy> queue(capacity);

    std::atomic<bool> start_flag{false};
    std::atomic<bool> stop_flag{false};
    std::atomic<uint64_t> pushed_count{0};
    std::atomic<uint64_t> popped_count{0};
    std::atomic<bool> order_valid{true};

    // Producer thread (t1)
    std::thread t1([&]() {
        while (!start_flag.load(std::memory_order_acquire)) {
            CPU_PAUSE();
        }

        uint64_t seq = 0;
        Object obj;
        while (!stop_flag.load(std::memory_order_relaxed)) {
            obj.sequence = seq;
            obj.timestamp = seq * 2;
            while (!queue.push(obj)) {
                if (stop_flag.load(std::memory_order_relaxed)) break;
                CPU_PAUSE();
            }
            ++seq;
        }
        pushed_count.store(seq, std::memory_order_release);
    });

    // Consumer thread (t2)
    std::thread t2([&]() {
        while (!start_flag.load(std::memory_order_acquire)) {
            CPU_PAUSE();
        }

        uint64_t expected_seq = 0;
        Object obj;
        while (!stop_flag.load(std::memory_order_relaxed)) {
            if (queue.pop(obj)) {
                if (obj.sequence != expected_seq) {
                    order_valid.store(false, std::memory_order_relaxed);
                }
                ++expected_seq;
            } else {
                CPU_PAUSE();
            }
        }

        // Drain phase: pop any remaining objects after stop flag
        while (queue.pop(obj)) {
            if (obj.sequence != expected_seq) {
                order_valid.store(false, std::memory_order_relaxed);
            }
            ++expected_seq;
        }

        popped_count.store(expected_seq, std::memory_order_release);
    });

    // Coordinate start
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto t_start = std::chrono::steady_clock::now();
    start_flag.store(true, std::memory_order_release);

    // Run for exactly duration_sec
    std::this_thread::sleep_for(std::chrono::duration<double>(duration_sec));

    stop_flag.store(true, std::memory_order_release);
    const auto t_stop = std::chrono::steady_clock::now();

    // t1.join()  t2.join()
    t1.join();
    t2.join();

    const double elapsed = std::chrono::duration<double>(t_stop - t_start).count();
    const uint64_t pushed = pushed_count.load(std::memory_order_acquire);
    const uint64_t popped = popped_count.load(std::memory_order_acquire);
    const double ops_per_sec = static_cast<double>(popped) / elapsed;
    const double mb_per_sec = (popped * sizeof(Object)) / (1024.0 * 1024.0 * elapsed);

    std::cout << "  Lock Policy: " << std::left << std::setw(12) << lock_name
              << " | Capacity: " << std::setw(6) << capacity
              << " | Pushed (1s): " << std::right << std::setw(10) << pushed
              << " | Popped (1s): " << std::right << std::setw(10) << popped
              << " | " << std::fixed << std::setprecision(2) << std::setw(8) << (ops_per_sec / 1e6) << " M ops/s"
              << " | " << std::fixed << std::setprecision(1) << std::setw(8) << mb_per_sec << " MB/s"
              << " | FIFO: " << (order_valid.load() ? "PASSED" : "FAILED")
              << "\n";
}

void test_sanity() {
    std::cout << "[Test] Running queue validation... ";
    SPSCQueue<Object, SpinLock> q(1024);
    Object in(123), out;
    assert(q.push(in) == true);
    assert(q.pop(out) == true);
    assert(out.sequence == 123);
    assert(q.pop(out) == false); // Empty
    std::cout << "PASSED\n";
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--test") {
        test_sanity();
        return 0;
    }

    std::cout << "===================================================================================================\n";
    std::cout << "   SPSC QUEUE BENCHMARK: 64-BYTE OBJECTS PUSH/POP IN 1 SECOND\n";
    std::cout << "   Roll No: 24bcs10096 | Name: Subhankar Parida\n";
    std::cout << "   Object Size: " << sizeof(Object) << " bytes (aligned to " << alignof(Object) << " bytes)\n";
    std::cout << "===================================================================================================\n\n";

    std::cout << "[Benchmark 1: Capacity = 1,024 slots]\n";
    run_benchmark<SpinLock>("SpinLock", 1024, 1.0);
    run_benchmark<std::mutex>("std::mutex", 1024, 1.0);

    std::cout << "\n[Benchmark 2: Capacity = 65,536 slots (High-Throughput Pool)]\n";
    run_benchmark<SpinLock>("SpinLock", 65536, 1.0);
    run_benchmark<std::mutex>("std::mutex", 65536, 1.0);

    std::cout << "\n===================================================================================================\n";
    std::cout << "All benchmarks completed and threads successfully joined (t1.join, t2.join).\n";
    return 0;
}
