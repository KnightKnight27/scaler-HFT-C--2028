#include <iostream>
#include <iomanip>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <memory>
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

// 64-Byte Cache-Aligned Order Packet
struct alignas(64) OrderPacket {
    std::uint64_t orderId;
    std::uint64_t timestampNs;
    std::uint64_t price;
    std::uint32_t quantity;
    std::uint32_t traderId;
    char client[16];
    std::uint8_t side;
    std::uint8_t flags;
    std::uint8_t padding[14];
};

static_assert(sizeof(OrderPacket) == 64, "OrderPacket must be exactly 64 bytes");

// Low-overhead Test-and-Set Spinlock
class Spinlock {
public:
    void lock() noexcept {
        while (flag_.test_and_set(std::memory_order_acquire)) {
#if defined(__x86_64__) || defined(_M_X64)
            _mm_pause();
#endif
        }
    }

    void unlock() noexcept {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

// SPSC Queue with std::mutex
template <typename T, std::size_t Capacity>
class MutexSPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr std::size_t kMask = Capacity - 1;

public:
    MutexSPSCQueue() : buffer_(std::make_unique<T[]>(Capacity)) {}

    bool push(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tail_ - head_ == Capacity) {
            return false;
        }
        buffer_[tail_ & kMask] = value;
        ++tail_;
        return true;
    }

    bool pop(T& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (head_ == tail_) {
            return false;
        }
        out = buffer_[head_ & kMask];
        ++head_;
        return true;
    }

private:
    std::mutex mutex_;
    std::size_t head_{0};
    std::size_t tail_{0};
    std::unique_ptr<T[]> buffer_;
};

// SPSC Queue with Spinlock
template <typename T, std::size_t Capacity>
class SpinlockSPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr std::size_t kMask = Capacity - 1;

public:
    SpinlockSPSCQueue() : buffer_(std::make_unique<T[]>(Capacity)) {}

    bool push(const T& value) {
        spinlock_.lock();
        if (tail_ - head_ == Capacity) {
            spinlock_.unlock();
            return false;
        }
        buffer_[tail_ & kMask] = value;
        ++tail_;
        spinlock_.unlock();
        return true;
    }

    bool pop(T& out) {
        spinlock_.lock();
        if (head_ == tail_) {
            spinlock_.unlock();
            return false;
        }
        out = buffer_[head_ & kMask];
        ++head_;
        spinlock_.unlock();
        return true;
    }

private:
    Spinlock spinlock_;
    std::size_t head_{0};
    std::size_t tail_{0};
    std::unique_ptr<T[]> buffer_;
};

// Lock-Free SPSC Queue with Cache-Line Aligned Atomic Indices (Comparative Baseline)
template <typename T, std::size_t Capacity>
class LockFreeSPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr std::size_t kMask = Capacity - 1;

public:
    LockFreeSPSCQueue() : buffer_(std::make_unique<T[]>(Capacity)) {}

    bool push(const T& value) {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        if (t - head_.load(std::memory_order_acquire) == Capacity) {
            return false;
        }
        buffer_[t & kMask] = value;
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& out) {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        if (h == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        out = buffer_[h & kMask];
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::atomic<std::size_t> tail_{0};
    alignas(64) std::atomic<std::size_t> head_{0};
    std::unique_ptr<T[]> buffer_;
};

struct BenchmarkResult {
    const char* name;
    std::uint64_t totalOps;
    double elapsedSeconds;
    double opsPerSec;
    double megabytesPerSec;
    double latencyNsPerOp;
};

template <typename QueueType>
BenchmarkResult runOneSecondBenchmark(const char* name) {
    constexpr std::chrono::seconds kDuration(1);
    QueueType queue;

    std::atomic<bool> isRunning{true};
    std::atomic<bool> producerDone{false};
    std::uint64_t pushCount{0};
    std::uint64_t popCount{0};

    // Producer Thread (t1)
    std::thread t1([&]() {
        OrderPacket packet{};
        packet.timestampNs = 1718000000000ULL;
        packet.price = 25050;
        packet.quantity = 100;
        packet.traderId = 1;
        std::memcpy(packet.client, "Bish", 5);
        packet.side = 1;
        packet.flags = 0;

        while (isRunning.load(std::memory_order_relaxed)) {
            packet.orderId = pushCount + 1;
            if (queue.push(packet)) {
                ++pushCount;
            } else {
#if defined(__x86_64__) || defined(_M_X64)
                _mm_pause();
#endif
            }
        }
        producerDone.store(true, std::memory_order_release);
    });

    // Consumer Thread (t2)
    std::thread t2([&]() {
        OrderPacket outPacket{};
        while (true) {
            if (queue.pop(outPacket)) {
                ++popCount;
            } else if (producerDone.load(std::memory_order_acquire)) {
                if (!queue.pop(outPacket)) {
                    break;
                }
                ++popCount;
            } else {
#if defined(__x86_64__) || defined(_M_X64)
                _mm_pause();
#endif
            }
        }
    });

    const auto startTime = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(kDuration);
    isRunning.store(false, std::memory_order_release);

    t1.join();
    t2.join();
    const auto endTime = std::chrono::steady_clock::now();

    assert(pushCount == popCount);

    const std::chrono::duration<double> elapsed = endTime - startTime;
    const double elapsedSec = elapsed.count();
    const double opsSec = static_cast<double>(popCount) / elapsedSec;
    const double mbSec = (static_cast<double>(popCount) * sizeof(OrderPacket)) / (elapsedSec * 1024.0 * 1024.0);
    const double latencyNs = (elapsedSec * 1e9) / static_cast<double>(popCount);

    return BenchmarkResult{
        name,
        popCount,
        elapsedSec,
        opsSec,
        mbSec,
        latencyNs
    };
}

void printResultTable(const std::array<BenchmarkResult, 3>& results) {
    std::cout << "\n========================================================================================\n";
    std::cout << "                 SPSC QUEUE 64-BYTE OBJECT 1-SECOND BENCHMARK SPECS                     \n";
    std::cout << "========================================================================================\n";
    std::cout << std::left
              << std::setw(24) << "Synchronization Mode"
              << std::setw(16) << "Total Ops/1s"
              << std::setw(18) << "Throughput (Mops/s)"
              << std::setw(18) << "Bandwidth (MB/s)"
              << std::setw(16) << "Avg Latency (ns)"
              << "\n";
    std::cout << "----------------------------------------------------------------------------------------\n";

    for (const auto& r : results) {
        std::cout << std::left
                  << std::setw(24) << r.name
                  << std::setw(16) << r.totalOps
                  << std::setw(18) << std::fixed << std::setprecision(2) << (r.opsPerSec / 1e6)
                  << std::setw(18) << std::fixed << std::setprecision(2) << r.megabytesPerSec
                  << std::setw(16) << std::fixed << std::setprecision(2) << r.latencyNsPerOp
                  << "\n";
    }
    std::cout << "========================================================================================\n\n";
}

int main() {
    constexpr std::size_t kCapacity = 65536; // 64K slots

    std::cout << "Starting SPSC 64-byte benchmarks (1 second per test)...\n";

    const auto resMutex = runOneSecondBenchmark<MutexSPSCQueue<OrderPacket, kCapacity>>("std::mutex");
    const auto resSpinlock = runOneSecondBenchmark<SpinlockSPSCQueue<OrderPacket, kCapacity>>("Spinlock (atomic_flag)");
    const auto resLockFree = runOneSecondBenchmark<LockFreeSPSCQueue<OrderPacket, kCapacity>>("Lock-Free (acquire/rel)");

    const std::array<BenchmarkResult, 3> results = {resMutex, resSpinlock, resLockFree};
    printResultTable(results);

    return 0;
}
