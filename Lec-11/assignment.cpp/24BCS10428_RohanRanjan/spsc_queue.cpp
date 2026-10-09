// SPSC queue (lock based) + 1 second throughput benchmark
//
// - fixed-capacity ring buffer, all slots preallocated up front (memory pool,
//   no new/delete on the hot path)
// - payload is a 64 byte object (one cache line)
// - lock is a template param: SpinLock (atomic_flag busy-wait) or std::mutex
// - lock-free version included only as a baseline to compare against
//
// build: g++ -std=c++17 -O2 -pthread spsc_queue.cpp -o spsc && ./spsc

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define CPU_RELAX() _mm_pause()
#elif defined(__aarch64__)
#define CPU_RELAX() asm volatile("yield")
#else
#define CPU_RELAX() ((void)0)
#endif

// ---------------- payload ----------------
struct alignas(64) Order {
    uint64_t seq;
    uint64_t price;
    uint64_t qty;
    char     symbol[40];
};
static_assert(sizeof(Order) == 64, "Order must be exactly 64 bytes");

// ---------------- spinlock ----------------
class SpinLock {
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
public:
    void lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) CPU_RELAX();
    }
    void unlock() { flag_.clear(std::memory_order_release); }
};

// ---------------- memory pool ----------------
// one contiguous, cache-line aligned block of T, allocated once
template <typename T>
class MemoryPool {
    T*     slots_;
    size_t n_;
public:
    explicit MemoryPool(size_t n) : n_(n) {
        slots_ = static_cast<T*>(std::aligned_alloc(64, n * sizeof(T)));
        if (!slots_) { std::perror("aligned_alloc"); std::exit(1); }
        std::memset(static_cast<void*>(slots_), 0, n * sizeof(T)); // pre-fault pages
    }
    ~MemoryPool() { std::free(slots_); }
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    T& operator[](size_t i) { return slots_[i]; }
    size_t size() const { return n_; }
};

// ---------------- lock based SPSC queue ----------------
template <typename T, typename Lock>
class LockedSPSCQueue {
    MemoryPool<T> buf_;
    size_t        mask_;
    size_t        head_ = 0; // next slot to pop
    size_t        tail_ = 0; // next slot to push
    Lock          lock_;
public:
    explicit LockedSPSCQueue(size_t cap_pow2) : buf_(cap_pow2), mask_(cap_pow2 - 1) {}

    bool push(const T& v) {
        std::lock_guard<Lock> g(lock_);
        if (tail_ - head_ == buf_.size()) return false; // full
        buf_[tail_ & mask_] = v;
        ++tail_;
        return true;
    }

    bool pop(T& out) {
        std::lock_guard<Lock> g(lock_);
        if (head_ == tail_) return false; // empty
        out = buf_[head_ & mask_];
        ++head_;
        return true;
    }
};

// ---------------- lock-free SPSC queue (baseline only) ----------------
template <typename T>
class LockFreeSPSCQueue {
    MemoryPool<T> buf_;
    size_t        mask_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) size_t cached_tail_ = 0;   // consumer's copy of tail
    alignas(64) std::atomic<size_t> tail_{0};
    alignas(64) size_t cached_head_ = 0;   // producer's copy of head
public:
    explicit LockFreeSPSCQueue(size_t cap_pow2) : buf_(cap_pow2), mask_(cap_pow2 - 1) {}

    bool push(const T& v) {
        size_t t = tail_.load(std::memory_order_relaxed);
        if (t - cached_head_ == buf_.size()) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (t - cached_head_ == buf_.size()) return false;
        }
        buf_[t & mask_] = v;
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& out) {
        size_t h = head_.load(std::memory_order_relaxed);
        if (h == cached_tail_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (h == cached_tail_) return false;
        }
        out = buf_[h & mask_];
        head_.store(h + 1, std::memory_order_release);
        return true;
    }
};

// ---------------- benchmark ----------------
struct Result {
    uint64_t pushed;
    uint64_t popped;
    double   secs;
    bool     ok;
};

template <typename Queue>
Result run_once(size_t capacity, std::chrono::milliseconds dur) {
    Queue q(capacity);
    std::atomic<bool> start{false}, stop{false};
    uint64_t pushed = 0, popped = 0;
    bool ok = true;

    std::thread producer([&] {
        Order o{};
        std::strcpy(o.symbol, "AAPL");
        uint64_t seq = 0;
        while (!start.load(std::memory_order_acquire)) CPU_RELAX();
        while (!stop.load(std::memory_order_relaxed)) {
            o.seq = seq; o.price = seq * 3; o.qty = seq & 1023;
            if (q.push(o)) ++seq;
        }
        pushed = seq;
    });

    std::thread consumer([&] {
        Order o;
        uint64_t expect = 0;
        while (!start.load(std::memory_order_acquire)) CPU_RELAX();
        while (!stop.load(std::memory_order_relaxed)) {
            if (q.pop(o)) {
                if (o.seq != expect || o.price != expect * 3) ok = false; // FIFO / data check
                ++expect;
            }
        }
        popped = expect;
    });

    auto t0 = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(dur);
    stop.store(true, std::memory_order_relaxed);
    producer.join();
    consumer.join();
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return {pushed, popped, secs, ok};
}

template <typename Queue>
void bench(const char* name, size_t capacity, int runs) {
    std::vector<double> rates;
    bool all_ok = true;
    for (int i = 0; i < runs; ++i) {
        Result r = run_once<Queue>(capacity, std::chrono::milliseconds(1000));
        all_ok &= r.ok;
        rates.push_back(r.popped / r.secs);
    }
    std::sort(rates.begin(), rates.end());
    double med = rates[rates.size() / 2];
    std::printf("%-22s min %8.2f M/s | median %8.2f M/s | max %8.2f M/s | %7.2f GB/s | fifo %s\n",
                name, rates.front() / 1e6, med / 1e6, rates.back() / 1e6,
                med * sizeof(Order) / 1e9, all_ok ? "OK" : "BROKEN");
}

int main(int argc, char** argv) {
    size_t capacity = 1 << 16;               // 65536 slots * 64 B = 4 MB pool
    int    runs     = argc > 1 ? std::atoi(argv[1]) : 5;

    std::printf("SPSC queue, 64-byte objects, capacity %zu, %d x 1s runs each\n", capacity, runs);
    std::printf("numbers = objects pushed AND popped (consumed) per second\n\n");
    bench<LockedSPSCQueue<Order, SpinLock>>("spinlock", capacity, runs);
    bench<LockedSPSCQueue<Order, std::mutex>>("std::mutex", capacity, runs);
    bench<LockFreeSPSCQueue<Order>>("lock-free (baseline)", capacity, runs);
    return 0;
}
