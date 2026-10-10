// =============================================================================
// SPSC (Single-Producer / Single-Consumer) Queue  —  Lec-11 assignment
// =============================================================================
//
//  * Bounded ring buffer of fixed 64-byte objects.
//  * Guarded by a SPINLOCK (std::atomic_flag, busy while-loop). A std::mutex
//    version is included too so the two can be compared.
//  * A simple MEMORY POOL: the ring buffer *is* the storage pool, so no
//    per-item new/delete happens on the hot path.
//  * One producer thread + one consumer thread, joined with t1.join()/t2.join().
//  * Runs for 1 second and reports how many 64-byte objects were pushed+popped.
//
//  Build:  g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc_queue
//  Run:    ./spsc_queue
// =============================================================================

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

// ---- the 64-byte payload ----------------------------------------------------
struct alignas(64) Object {
    uint8_t data[64];
};
static_assert(sizeof(Object) == 64, "Object must be exactly 64 bytes");

// ---- avoid false sharing between the two threads ----------------------------
constexpr size_t CACHELINE = 64;

// =============================================================================
// Spinlock-guarded SPSC ring buffer (the ring is our memory pool)
// =============================================================================
template <size_t Capacity>
class SpinlockSpscQueue {
public:
    bool push(const Object& obj) {
        while (lock_.test_and_set(std::memory_order_acquire)) { /* spin */ }
        bool ok = false;
        size_t next = (head_ + 1) % Capacity;
        if (next != tail_) {          // not full
            buffer_[head_] = obj;     // copy into the pool slot
            head_ = next;
            ok = true;
        }
        lock_.clear(std::memory_order_release);
        return ok;
    }

    bool pop(Object& out) {
        while (lock_.test_and_set(std::memory_order_acquire)) { /* spin */ }
        bool ok = false;
        if (tail_ != head_) {         // not empty
            out = buffer_[tail_];
            tail_ = (tail_ + 1) % Capacity;
            ok = true;
        }
        lock_.clear(std::memory_order_release);
        return ok;
    }

private:
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    alignas(CACHELINE) size_t head_ = 0;
    alignas(CACHELINE) size_t tail_ = 0;
    alignas(CACHELINE) Object buffer_[Capacity];   // <-- fixed-size memory pool
};

// =============================================================================
// std::mutex-guarded SPSC ring buffer (same storage, different lock)
// =============================================================================
template <size_t Capacity>
class MutexSpscQueue {
public:
    bool push(const Object& obj) {
        std::lock_guard<std::mutex> g(m_);
        size_t next = (head_ + 1) % Capacity;
        if (next == tail_) return false;
        buffer_[head_] = obj;
        head_ = next;
        return true;
    }

    bool pop(Object& out) {
        std::lock_guard<std::mutex> g(m_);
        if (tail_ == head_) return false;
        out = buffer_[tail_];
        tail_ = (tail_ + 1) % Capacity;
        return true;
    }

private:
    std::mutex m_;
    alignas(CACHELINE) size_t head_ = 0;
    alignas(CACHELINE) size_t tail_ = 0;
    alignas(CACHELINE) Object buffer_[Capacity];
};

// =============================================================================
// Benchmark: run producer + consumer for ~1 second, count objects moved.
// =============================================================================
template <typename Queue>
uint64_t run_benchmark(const char* name, double seconds = 1.0) {
    Queue* q = new Queue();                 // on the heap (it is large)
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> produced{0};
    std::atomic<uint64_t> consumed{0};

    // Producer: push 64-byte objects until told to stop.
    std::thread t1([&] {
        Object obj;
        std::memset(obj.data, 0xAB, sizeof(obj.data));
        uint64_t count = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            if (q->push(obj)) ++count;
        }
        produced.store(count, std::memory_order_relaxed);
    });

    // Consumer: pop objects until told to stop AND the queue is drained.
    std::thread t2([&] {
        Object obj;
        uint64_t count = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            if (q->pop(obj)) ++count;
        }
        while (q->pop(obj)) ++count;   // drain remainder
        consumed.store(count, std::memory_order_relaxed);
    });

    std::this_thread::sleep_for(
        std::chrono::milliseconds(static_cast<int>(seconds * 1000)));
    stop.store(true, std::memory_order_relaxed);

    t1.join();
    t2.join();

    uint64_t ops = consumed.load();
    double per_sec = ops / seconds;
    std::printf("%-22s : %12llu objects  (%.2f million ops/sec, %.2f GB/sec)\n",
                name,
                (unsigned long long)ops,
                per_sec / 1e6,
                (per_sec * sizeof(Object)) / 1e9);
    delete q;
    return ops;
}

int main() {
    constexpr size_t CAP = 1024;   // ring capacity (number of 64-byte slots)

    std::printf("SPSC queue throughput of 64-byte objects (1 second each)\n");
    std::printf("Ring capacity: %zu slots  (%zu KB pool)\n\n",
                CAP, (CAP * sizeof(Object)) / 1024);

    run_benchmark<SpinlockSpscQueue<CAP>>("spinlock (atomic_flag)");
    run_benchmark<MutexSpscQueue<CAP>>("std::mutex");

    return 0;
}
