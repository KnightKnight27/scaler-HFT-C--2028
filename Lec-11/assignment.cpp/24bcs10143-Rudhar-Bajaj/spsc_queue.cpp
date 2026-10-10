// Lec-11 assignment: SPSC queue guarded by a lock.
// One producer thread pushes 64-byte objects, one consumer thread pops them,
// and we count how many make the round trip in 1 second.
//
// The queue's storage is a fixed ring of preallocated slots (a tiny memory
// pool), so push/pop never touch the heap; we only copy 64 bytes in and out.
//
// Build: clang++ -std=c++20 -O2 -pthread spsc_queue.cpp -o spsc
// Run:   ./spsc            (default 5 trials of 1s each per lock)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
static inline void cpu_relax() { _mm_pause(); }
#elif defined(__aarch64__)
static inline void cpu_relax() { asm volatile("yield"); }
#else
static inline void cpu_relax() {}
#endif

// ---------- the 64-byte object we move through the queue ----------
struct alignas(64) Msg {
    uint64_t seq;          // producer sequence number, used to check FIFO order
    uint64_t payload[7];   // filler so the object is exactly one cache line
};
static_assert(sizeof(Msg) == 64, "Msg must be 64 bytes");

// ---------- locks ----------
// Test-and-test-and-set spinlock: spin on a plain load so we don't hammer the
// cache line with RMW ops while someone else holds it.
class SpinLock {
    std::atomic<bool> locked_{false};
public:
    void lock() {
        for (;;) {
            if (!locked_.exchange(true, std::memory_order_acquire)) return;
            while (locked_.load(std::memory_order_relaxed)) cpu_relax();
        }
    }
    void unlock() { locked_.store(false, std::memory_order_release); }
};

// ---------- bounded SPSC ring buffer protected by Lock ----------
template <typename T, typename Lock>
class LockedSpscQueue {
public:
    explicit LockedSpscQueue(size_t capacity) : buf_(capacity), cap_(capacity) {}

    bool try_push(const T& v) {
        std::lock_guard<Lock> g(lock_);
        if (size_ == cap_) return false;      // full
        buf_[tail_] = v;
        tail_ = (tail_ + 1 == cap_) ? 0 : tail_ + 1;
        ++size_;
        return true;
    }

    bool try_pop(T& out) {
        std::lock_guard<Lock> g(lock_);
        if (size_ == 0) return false;          // empty
        out = buf_[head_];
        head_ = (head_ + 1 == cap_) ? 0 : head_ + 1;
        --size_;
        return true;
    }

private:
    std::vector<T> buf_;   // preallocated slot pool
    size_t cap_;
    size_t head_ = 0, tail_ = 0, size_ = 0;
    alignas(64) Lock lock_;
};

// ---------- lock-free baseline (for comparison only) ----------
// Classic SPSC: producer owns tail, consumer owns head; acquire/release on the
// indices is all the synchronisation needed. Each index sits on its own cache
// line, and each side caches the other's index to avoid needless cross-core reads.
template <typename T>
class LockFreeSpscQueue {
public:
    explicit LockFreeSpscQueue(size_t capacity) : buf_(capacity + 1), cap_(capacity + 1) {}

    bool try_push(const T& v) {
        size_t t = tail_.load(std::memory_order_relaxed);
        size_t next = (t + 1 == cap_) ? 0 : t + 1;
        if (next == head_cache_) {
            head_cache_ = head_.load(std::memory_order_acquire);
            if (next == head_cache_) return false;
        }
        buf_[t] = v;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    bool try_pop(T& out) {
        size_t h = head_.load(std::memory_order_relaxed);
        if (h == tail_cache_) {
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (h == tail_cache_) return false;
        }
        out = buf_[h];
        head_.store((h + 1 == cap_) ? 0 : h + 1, std::memory_order_release);
        return true;
    }

private:
    std::vector<T> buf_;
    size_t cap_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) size_t tail_cache_ = 0;   // consumer's copy of tail
    alignas(64) std::atomic<size_t> tail_{0};
    alignas(64) size_t head_cache_ = 0;   // producer's copy of head
};

// ---------- benchmark ----------
struct Result {
    uint64_t popped;
    double seconds;
    bool order_ok;
};

template <typename Queue>
Result run_once(size_t capacity, std::chrono::milliseconds duration) {
    Queue q(capacity);
    // Each shared flag/result on its own cache line so the threads don't
    // false-share with each other; counters stay local inside each thread.
    alignas(64) std::atomic<bool> go{false};
    alignas(64) std::atomic<bool> stop{false};
    alignas(64) uint64_t pushed = 0;
    alignas(64) uint64_t popped = 0;
    alignas(64) bool order_ok = true;

    std::thread producer([&] {
        Msg m{};
        uint64_t n = 0;
        while (!go.load(std::memory_order_acquire)) cpu_relax();
        while (!stop.load(std::memory_order_relaxed)) {
            m.seq = n;
            m.payload[0] = n * 31;
            if (q.try_push(m)) ++n;
        }
        pushed = n;
    });

    std::thread consumer([&] {
        Msg m;
        uint64_t expect = 0;
        bool ok = true;
        while (!go.load(std::memory_order_acquire)) cpu_relax();
        while (!stop.load(std::memory_order_relaxed)) {
            if (q.try_pop(m)) {
                if (m.seq != expect || m.payload[0] != expect * 31) ok = false;
                ++expect;
            }
        }
        popped = expect;
        order_ok = ok;
    });

    auto t0 = std::chrono::steady_clock::now();
    go.store(true, std::memory_order_release);
    std::this_thread::sleep_for(duration);
    stop.store(true, std::memory_order_relaxed);
    auto t1 = std::chrono::steady_clock::now();

    producer.join();
    consumer.join();

    // Whatever is still sitting in the ring was pushed but not popped.
    if (pushed < popped) order_ok = false;
    return {popped, std::chrono::duration<double>(t1 - t0).count(), order_ok};
}

template <typename Queue>
void bench(const char* name, size_t capacity, int trials) {
    std::vector<double> rates;
    bool all_ok = true;
    for (int i = 0; i < trials; ++i) {
        Result r = run_once<Queue>(capacity, std::chrono::milliseconds(1000));
        rates.push_back(r.popped / r.seconds);
        all_ok &= r.order_ok;
    }
    std::sort(rates.begin(), rates.end());
    double median = rates[rates.size() / 2];
    std::printf("%-22s cap=%-6zu min=%6.2f M/s  median=%6.2f M/s  max=%6.2f M/s  "
                "(%.0f MB/s)  order=%s\n",
                name, capacity, rates.front() / 1e6, median / 1e6, rates.back() / 1e6,
                median * sizeof(Msg) / 1e6, all_ok ? "OK" : "BROKEN");
}

int main(int argc, char** argv) {
    int trials = argc > 1 ? std::atoi(argv[1]) : 5;
    if (trials < 1) trials = 1;

    std::printf("SPSC queue, 64-byte objects, 1 producer + 1 consumer, 1s per trial, "
                "%d trials\n", trials);
    std::printf("hardware threads: %u\n\n", std::thread::hardware_concurrency());

    for (size_t cap : {1024, 65536}) {
        bench<LockedSpscQueue<Msg, SpinLock>>("spinlock", cap, trials);
        bench<LockedSpscQueue<Msg, std::mutex>>("std::mutex", cap, trials);
        bench<LockFreeSpscQueue<Msg>>("lock-free (baseline)", cap, trials);
        std::printf("\n");
    }
    return 0;
}
