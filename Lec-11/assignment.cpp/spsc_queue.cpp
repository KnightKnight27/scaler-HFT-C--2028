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

// ---------------------------------------------------------------------------
// Solution
//   * Ring buffer over a fixed, pre-allocated block of 64-byte slots
//     (the "memory pool": no malloc/free on the hot path).
//   * Same queue, three interchangeable synchronisation policies:
//       - std::mutex          (OS lock, sleeps under contention)
//       - SpinLock            (atomic_flag busy-wait)
//       - NoLock              (lock-free SPSC with acquire/release atomics,
//                              included only as a baseline to compare against)
//   * One producer thread + one consumer thread, run for 1 second each.
//
// Build:  g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc_queue
// Run:    ./spsc_queue            (3 runs per variant by default)
//         ./spsc_queue 5          (5 runs per variant)
// ---------------------------------------------------------------------------

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

// Exactly one cache line. The payload is real data (sequence number + filler)
// so the compiler cannot optimise the copies away.
struct alignas(64) Obj {
    uint64_t seq;
    uint64_t filler[7];
};
static_assert(sizeof(Obj) == 64, "Obj must be exactly 64 bytes");

// ----------------------------- lock policies -------------------------------

struct MutexLock {
    std::mutex m;
    void lock()   { m.lock(); }
    void unlock() { m.unlock(); }
};

struct SpinLock {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
#if defined(__aarch64__)
            __asm__ volatile("yield");
#elif defined(__x86_64__)
            __asm__ volatile("pause");
#endif
        }
    }
    void unlock() { flag.clear(std::memory_order_release); }
};

// ------------------------------- the queue ---------------------------------

template <typename T, typename Lock, size_t Capacity = 1024>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    SpscQueue() : mPool(new T[Capacity]) {}  // the memory pool: allocated once

    bool push(const T& v) {
        std::lock_guard<Lock> g(mLock);
        if (mCount == Capacity) return false;            // full
        mPool[mHead & (Capacity - 1)] = v;
        ++mHead;
        ++mCount;
        return true;
    }

    bool pop(T& out) {
        std::lock_guard<Lock> g(mLock);
        if (mCount == 0) return false;                   // empty
        out = mPool[mTail & (Capacity - 1)];
        ++mTail;
        --mCount;
        return true;
    }

private:
    Lock mLock;
    std::unique_ptr<T[]> mPool;
    size_t mHead{0};
    size_t mTail{0};
    size_t mCount{0};
};

// Lock-free baseline: only valid because there is exactly 1 producer and 1
// consumer. Head is written by the producer only, tail by the consumer only.
template <typename T, size_t Capacity = 1024>
class SpscQueueLockFree {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    SpscQueueLockFree() : mPool(new T[Capacity]) {}

    // Each side keeps a private cached copy of the other side's index and only
    // re-reads the shared atomic when the cache says full/empty. This avoids
    // bouncing the cache line on every single call.
    bool push(const T& v) {
        size_t h = mHead.load(std::memory_order_relaxed);
        if (h - mCachedTail == Capacity) {
            mCachedTail = mTail.load(std::memory_order_acquire);
            if (h - mCachedTail == Capacity) return false;
        }
        mPool[h & (Capacity - 1)] = v;
        mHead.store(h + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& out) {
        size_t t = mTail.load(std::memory_order_relaxed);
        if (t == mCachedHead) {
            mCachedHead = mHead.load(std::memory_order_acquire);
            if (t == mCachedHead) return false;
        }
        out = mPool[t & (Capacity - 1)];
        mTail.store(t + 1, std::memory_order_release);
        return true;
    }

private:
    std::unique_ptr<T[]> mPool;
    // Every field below lives on its own cache line: no false sharing.
    alignas(64) std::atomic<size_t> mHead{0};   // written by producer
    alignas(64) size_t mCachedTail{0};          // producer-private
    alignas(64) std::atomic<size_t> mTail{0};   // written by consumer
    alignas(64) size_t mCachedHead{0};          // consumer-private
};

// ------------------------------- benchmark ---------------------------------

struct Result {
    uint64_t pushed = 0;
    uint64_t popped = 0;
    uint64_t orderErrors = 0;
    double seconds = 0;
};

template <typename Queue>
Result runOneSecond(Queue& q) {
    std::atomic<bool> go{false};
    std::atomic<bool> stop{false};
    Result r;

    std::thread producer([&] {
        Obj o{};
        uint64_t seq = 0;
        while (!go.load(std::memory_order_acquire)) {}
        while (!stop.load(std::memory_order_relaxed)) {
            o.seq = seq;
            if (q.push(o)) ++seq;
        }
        r.pushed = seq;
    });

    std::thread consumer([&] {
        Obj o{};
        uint64_t expected = 0;
        uint64_t errors = 0;
        while (!go.load(std::memory_order_acquire)) {}
        while (!stop.load(std::memory_order_relaxed)) {
            if (q.pop(o)) {
                if (o.seq != expected) ++errors;   // FIFO order check
                ++expected;
            }
        }
        r.popped = expected;
        r.orderErrors = errors;
    });

    auto t0 = std::chrono::steady_clock::now();
    go.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop.store(true, std::memory_order_relaxed);
    producer.join();
    consumer.join();
    auto t1 = std::chrono::steady_clock::now();

    r.seconds = std::chrono::duration<double>(t1 - t0).count();
    return r;
}

template <typename MakeQueue>
void bench(const char* name, int runs, MakeQueue make) {
    std::vector<double> perSec;
    uint64_t errors = 0;
    std::printf("\n== %s ==\n", name);
    for (int i = 0; i < runs; ++i) {
        auto q = make();
        Result r = runOneSecond(*q);
        double ops = r.popped / r.seconds;
        perSec.push_back(ops);
        errors += r.orderErrors;
        std::printf("  run %d: pushed=%llu popped=%llu  -> %.2f M objs/s  (%.2f GB/s of 64B payload)\n",
                    i + 1,
                    (unsigned long long)r.pushed, (unsigned long long)r.popped,
                    ops / 1e6, ops * sizeof(Obj) / 1e9);
    }
    std::sort(perSec.begin(), perSec.end());
    double median = perSec[perSec.size() / 2];
    std::printf("  median: %.2f M objs/s | best: %.2f M objs/s | FIFO order errors: %llu\n",
                median / 1e6, perSec.back() / 1e6, (unsigned long long)errors);
}

int main(int argc, char** argv) {
    int runs = (argc > 1) ? std::atoi(argv[1]) : 3;
    std::printf("sizeof(Obj) = %zu bytes, queue capacity = 1024 slots, hw threads = %u\n",
                sizeof(Obj), std::thread::hardware_concurrency());

    bench("std::mutex SPSC queue", runs,
          [] { return std::make_unique<SpscQueue<Obj, MutexLock>>(); });
    bench("SpinLock SPSC queue", runs,
          [] { return std::make_unique<SpscQueue<Obj, SpinLock>>(); });
    bench("Lock-free SPSC queue (baseline, no lock)", runs,
          [] { return std::make_unique<SpscQueueLockFree<Obj>>(); });
    return 0;
}
