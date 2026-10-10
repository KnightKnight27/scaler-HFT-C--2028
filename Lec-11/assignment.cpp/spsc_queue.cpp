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
#include <vector>
#include <array>
#include <iomanip>

// 64-byte payload aligned to a cache line
struct alignas(64) Object64 {
    uint64_t seq{0};
    uint8_t payload[56]{0};
};
static_assert(sizeof(Object64) == 64, "Object64 must be exactly 64 bytes");

// Simple SpinLock using atomic_flag test-and-set while-loop
class SpinLock {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
public:
    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
            #if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
            #if defined(_MSC_VER)
            _mm_pause();
            #elif defined(__GNUC__)
            __builtin_ia32_pause();
            #endif
            #endif
        }
    }
    void unlock() {
        flag.clear(std::memory_order_release);
    }
};

// SPSC Queue with compile-time Lock policy
template <typename LockType, std::size_t Capacity = 4096>
class SPSCQueueWithLock {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr std::size_t kMask = Capacity - 1;

    std::array<Object64, Capacity> buffer_;
    std::size_t head_{0}; // Consumer read index
    std::size_t tail_{0}; // Producer write index
    std::size_t count_{0};
    LockType lock_;

public:
    bool push(const Object64& item) {
        std::unique_lock<LockType> guard(lock_);
        if (count_ == Capacity) {
            return false; // Queue full
        }
        buffer_[tail_ & kMask] = item;
        tail_++;
        count_++;
        return true;
    }

    bool pop(Object64& outItem) {
        std::unique_lock<LockType> guard(lock_);
        if (count_ == 0) {
            return false; // Queue empty
        }
        outItem = buffer_[head_ & kMask];
        head_++;
        count_--;
        return true;
    }
};

struct BenchmarkResult {
    uint64_t pushes{0};
    uint64_t pops{0};
    double durationSec{0.0};
};

template <typename LockType>
BenchmarkResult runBenchmark(const std::string& name) {
    SPSCQueueWithLock<LockType, 4096> queue;
    std::atomic<bool> startFlag{false};
    std::atomic<bool> stopFlag{false};

    uint64_t pushCount = 0;
    uint64_t popCount = 0;

    std::thread t1([&]() {
        while (!startFlag.load(std::memory_order_acquire)) {
            // Wait for signal
        }
        Object64 item;
        while (!stopFlag.load(std::memory_order_relaxed)) {
            item.seq = pushCount + 1;
            if (queue.push(item)) {
                pushCount++;
            }
        }
    });

    std::thread t2([&]() {
        while (!startFlag.load(std::memory_order_acquire)) {
            // Wait for signal
        }
        Object64 popped;
        while (!stopFlag.load(std::memory_order_relaxed)) {
            if (queue.pop(popped)) {
                popCount++;
            }
        }
    });

    // Start benchmark window
    auto startTime = std::chrono::high_resolution_clock::now();
    startFlag.store(true, std::memory_order_release);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    stopFlag.store(true, std::memory_order_release);
    auto endTime = std::chrono::high_resolution_clock::now();

    t1.join();
    t2.join();

    double elapsed = std::chrono::duration<double>(endTime - startTime).count();

    std::cout << "==================================================\n";
    std::cout << "Lock Policy: " << name << "\n";
    std::cout << "Elapsed Time:      " << std::fixed << std::setprecision(4) << elapsed << " s\n";
    std::cout << "Successful Pushes: " << pushCount << " (" << static_cast<uint64_t>(pushCount / elapsed) << " ops/s)\n";
    std::cout << "Successful Pops:   " << popCount << " (" << static_cast<uint64_t>(popCount / elapsed) << " ops/s)\n";
    std::cout << "Total Throughput:  " << (pushCount + popCount) << " ops (" 
              << static_cast<uint64_t>((pushCount + popCount) / elapsed) << " ops/s)\n";
    std::cout << "==================================================\n\n";

    return {pushCount, popCount, elapsed};
}

int main() {
    std::cout << "Running SPSC Queue 64-byte Object 1-Second Benchmark...\n\n";

    // Warm-up run
    {
        SPSCQueueWithLock<SpinLock, 1024> warmQ;
        Object64 obj;
        warmQ.push(obj);
        warmQ.pop(obj);
    }

    std::cout << "--- Test 1: SpinLock (Atomic Flag While-Loop) ---\n";
    auto spinResult = runBenchmark<SpinLock>("SpinLock (Atomic Flag)");

    std::cout << "--- Test 2: std::mutex ---\n";
    auto mutexResult = runBenchmark<std::mutex>("std::mutex");

    return 0;
}
