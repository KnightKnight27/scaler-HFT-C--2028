// WRITE AN SPSC QUEUE
// SPINLOCK ( WHILE LOOP) OR STD::MUTEX
// t1.join()  t2.join()
// producer consumer to push objects and pop objects
//
// you need to figure out a way that with locks how many
// 64 byte objects can u push and pop in 1 second
//  raise a git PR for the same
//  add readme for ur per second specs
//
// ^^ MEMORY POOL ^^
//
// Build: clang++ -std=c++17 -O3 -pthread spsc_queue.cpp -o spsc_queue
// Run:   ./spsc_queue

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
inline void cpu_relax() { _mm_pause(); }
#elif defined(__aarch64__)
inline void cpu_relax() { asm volatile("yield" ::: "memory"); }
#else
inline void cpu_relax() {}
#endif

constexpr std::size_t kCacheLine = 64;

// The object we move through the queue: exactly one cache line.
struct alignas(kCacheLine) Order {
    std::uint64_t id;
    std::uint64_t price;
    std::uint32_t qty;
    std::uint32_t side;
    char symbol[40];
};
static_assert(sizeof(Order) == 64, "Order must be 64 bytes");

// ---------------------------------------------------------------------------
// Locks
// ---------------------------------------------------------------------------

// Spinlock: a plain while loop on an atomic flag (test-and-test-and-set).
class SpinLock {
    std::atomic<bool> flag_{false};

public:
    void lock() {
        while (flag_.exchange(true, std::memory_order_acquire)) {
            while (flag_.load(std::memory_order_relaxed)) cpu_relax();
        }
    }
    void unlock() { flag_.store(false, std::memory_order_release); }
};

// ---------------------------------------------------------------------------
// Memory pool: one contiguous, pre-allocated slab of Order slots.
// The queue never calls new/delete on the hot path, it only reuses slots.
// ---------------------------------------------------------------------------
template <std::size_t N>
struct OrderPool {
    static_assert((N & (N - 1)) == 0, "pool size must be a power of two");
    alignas(kCacheLine) Order slots[N];
    static constexpr std::size_t mask = N - 1;
    Order& at(std::size_t i) { return slots[i & mask]; }
};

// ---------------------------------------------------------------------------
// SPSC ring buffer guarded by a lock (SpinLock or std::mutex).
// ---------------------------------------------------------------------------
template <typename Lock, std::size_t N>
class LockedSpscQueue {
    OrderPool<N> pool_;
    alignas(kCacheLine) Lock lock_;
    std::size_t head_ = 0;  // next slot to pop
    std::size_t tail_ = 0;  // next slot to push

public:
    bool push(const Order& o) {
        std::lock_guard<Lock> guard(lock_);
        if (tail_ - head_ == N) return false;  // full
        pool_.at(tail_++) = o;
        return true;
    }

    bool pop(Order& out) {
        std::lock_guard<Lock> guard(lock_);
        if (head_ == tail_) return false;  // empty
        out = pool_.at(head_++);
        return true;
    }
};

// ---------------------------------------------------------------------------
// Lock-free SPSC ring buffer, for comparison against the locked versions.
// ---------------------------------------------------------------------------
template <std::size_t N>
class LockFreeSpscQueue {
    OrderPool<N> pool_;
    alignas(kCacheLine) std::atomic<std::size_t> head_{0};
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};
    alignas(kCacheLine) std::size_t cached_head_ = 0;  // producer-local
    alignas(kCacheLine) std::size_t cached_tail_ = 0;  // consumer-local

public:
    bool push(const Order& o) {
        std::size_t t = tail_.load(std::memory_order_relaxed);
        if (t - cached_head_ == N) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (t - cached_head_ == N) return false;
        }
        pool_.at(t) = o;
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    bool pop(Order& out) {
        std::size_t h = head_.load(std::memory_order_relaxed);
        if (h == cached_tail_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (h == cached_tail_) return false;
        }
        out = pool_.at(h);
        head_.store(h + 1, std::memory_order_release);
        return true;
    }
};

// ---------------------------------------------------------------------------
// Benchmark: producer (t1) pushes, consumer (t2) pops, for one second.
// ---------------------------------------------------------------------------
constexpr std::size_t kQueueSize = 1 << 16;

struct Result {
    std::uint64_t pushed;
    std::uint64_t popped;
    double seconds;
    bool in_order;
};

template <typename Queue>
Result run_for_one_second() {
    auto q = std::make_unique<Queue>();
    std::atomic<bool> go{false}, stop{false};
    std::uint64_t pushed = 0, popped = 0;
    bool in_order = true;

    std::thread t1([&] {
        Order o{};
        o.price = 245050;  // in paise
        o.qty = 10;
        std::strcpy(o.symbol, "RELIANCE");
        while (!go.load(std::memory_order_acquire)) cpu_relax();
        while (!stop.load(std::memory_order_relaxed)) {
            o.id = pushed;
            if (q->push(o)) ++pushed;
        }
    });

    std::thread t2([&] {
        Order o{};
        while (!go.load(std::memory_order_acquire)) cpu_relax();
        while (!stop.load(std::memory_order_relaxed)) {
            if (q->pop(o)) {
                if (o.id != popped) in_order = false;
                ++popped;
            }
        }
    });

    auto start = std::chrono::steady_clock::now();
    go.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop.store(true, std::memory_order_relaxed);

    t1.join();
    t2.join();
    auto end = std::chrono::steady_clock::now();

    return {pushed, popped, std::chrono::duration<double>(end - start).count(), in_order};
}

template <typename Queue>
void report(const char* name) {
    Result r = run_for_one_second<Queue>();
    double pops_per_sec = r.popped / r.seconds;
    double mb_per_sec = pops_per_sec * sizeof(Order) / (1024.0 * 1024.0);
    std::printf("%-22s pushed %11llu  popped %11llu  in %.3fs  -> %6.2f M obj/s  %8.1f MB/s  %s\n",
                name, (unsigned long long)r.pushed, (unsigned long long)r.popped, r.seconds,
                pops_per_sec / 1e6, mb_per_sec, r.in_order ? "FIFO ok" : "FIFO BROKEN");
}

int main() {
    std::printf("SPSC queue, %zu-byte objects, %zu-slot pool, 1 second per run\n\n",
                sizeof(Order), kQueueSize);
    report<LockedSpscQueue<SpinLock, kQueueSize>>("spinlock (while loop)");
    report<LockedSpscQueue<std::mutex, kQueueSize>>("std::mutex");
    report<LockFreeSpscQueue<kQueueSize>>("lock-free (bonus)");
    return 0;
}
