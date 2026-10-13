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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#include <immintrin.h>
static inline void cpu_relax() { _mm_pause(); }
#elif defined(__aarch64__) || defined(__arm__)
static inline void cpu_relax() { asm volatile("yield" ::: "memory"); }
#else
static inline void cpu_relax() {}
#endif

using Clock = std::chrono::steady_clock;

// Sample order for the benchmark.
struct alignas(64) Order {
    uint64_t seq;        // used to check FIFO order
    uint64_t order_id;
    int64_t  price;      // in ticks
    uint32_t qty;
    uint32_t symbol_id;
    uint64_t timestamp;
    uint64_t check;      // seq ^ kCheckMagic, checks the sequence tag
    uint8_t  side;       // 0 = buy, 1 = sell
    uint8_t  pad[15];
};
static_assert(sizeof(Order) == 64, "Order must be exactly 64 bytes");

static constexpr uint64_t kCheckMagic = 0x9E3779B97F4A7C15ULL;

static Order make_order(uint64_t seq) {
    Order o{};
    o.seq       = seq;
    o.order_id  = seq + 1'000'000;
    o.price     = 10'000 + static_cast<int64_t>(seq % 100);
    o.qty       = static_cast<uint32_t>(seq % 500) + 1;
    o.symbol_id = static_cast<uint32_t>(seq % 64);
    o.timestamp = seq;
    o.check     = seq ^ kCheckMagic;
    o.side      = static_cast<uint8_t>(seq & 1);
    return o;
}

class SpinLock {
public:
    void lock() noexcept {
        while (locked_.exchange(true, std::memory_order_acquire)) {
            // Wait on loads rather than repeatedly exchanging while the lock is held.
            while (locked_.load(std::memory_order_relaxed)) cpu_relax();
        }
    }

    void unlock() noexcept { locked_.store(false, std::memory_order_release); }

private:
    std::atomic<bool> locked_{false};
};

// One ring buffer implementation for both lock types.
template <typename T, typename Lock>
class SPSCQueue {
public:
    // Round up so indices can wrap with a mask. The pool is allocated only once.
    explicit SPSCQueue(size_t capacity)
        : capacity_(round_up_pow2(capacity)),
          mask_(capacity_ - 1),
          pool_(new T[capacity_]) {}

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    bool try_push(const T& item) {
        std::lock_guard<Lock> guard(lock_);
        if (tail_ - head_ == capacity_) return false;
        pool_[tail_ & mask_] = item;
        ++tail_;
        return true;
    }

    bool try_pop(T& out) {
        std::lock_guard<Lock> guard(lock_);
        if (head_ == tail_) return false;
        out = pool_[head_ & mask_];
        ++head_;
        return true;
    }

    size_t capacity() const { return capacity_; }

private:
    static size_t round_up_pow2(size_t n) {
        size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    const size_t         capacity_;
    const size_t         mask_;
    std::unique_ptr<T[]> pool_;
    Lock                 lock_;
    size_t               head_ = 0;  // next slot to pop
    size_t               tail_ = 0;  // next slot to push
};

static void require(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "self-test FAILED: %s\n", what);
        std::exit(1);
    }
}

// Check empty, full and wraparound before starting the threads.
template <typename Lock>
static void self_test() {
    SPSCQueue<Order, Lock> q(3);  // rounds up to 4
    Order o;

    require(q.capacity() == 4, "capacity rounds up to a power of two");
    require(!q.try_pop(o), "pop from empty queue fails");

    for (uint64_t i = 0; i < 4; ++i) require(q.try_push(make_order(i)), "push into non-full queue");
    require(!q.try_push(make_order(99)), "push into full queue fails");

    // Keep the queue full while moving the indices around the pool.
    for (uint64_t i = 4; i < 20; ++i) {
        require(q.try_pop(o) && o.seq == i - 4, "FIFO order across wraparound");
        require(q.try_push(make_order(i)), "push after pop");
    }
    for (uint64_t i = 16; i < 20; ++i) {
        require(q.try_pop(o) && o.seq == i && o.check == (i ^ kCheckMagic), "drain in order");
    }
    require(!q.try_pop(o), "queue is empty after drain");
}

struct RunResult {
    uint64_t items;    // successfully popped objects
    double   seconds;  // includes draining and joining the threads
    bool     ok;       // sequence checks passed and pushed == popped
};

// Stop the producer after the requested time, then let the consumer finish.
template <typename Lock>
static RunResult run_once(size_t capacity, double seconds) {
    SPSCQueue<Order, Lock> q(capacity);

    std::atomic<bool> go{false};
    std::atomic<bool> stop{false};
    std::atomic<bool> producer_done{false};

    uint64_t pushed = 0;
    uint64_t popped = 0;
    bool     ok     = true;

    std::thread t1([&] {  // producer
        while (!go.load(std::memory_order_acquire)) cpu_relax();

        uint64_t seq = 0;
        Order    o   = make_order(seq);
        while (!stop.load(std::memory_order_relaxed)) {
            if (q.try_push(o)) {
                o = make_order(++seq);
            } else {
                cpu_relax();  // full: give the consumer a chance
            }
        }
        pushed = seq;
        producer_done.store(true, std::memory_order_release);
    });

    std::thread t2([&] {  // consumer
        while (!go.load(std::memory_order_acquire)) cpu_relax();

        Order    o;
        uint64_t expected = 0;
        auto consume = [&] {
            if (o.seq != expected || o.check != (o.seq ^ kCheckMagic)) ok = false;
            ++expected;
        };

        for (;;) {
            if (q.try_pop(o)) {
                consume();
                continue;
            }
            cpu_relax();  // empty: give the producer a chance
            if (producer_done.load(std::memory_order_acquire)) {
                // No more pushes can arrive, so drain the remaining items.
                while (q.try_pop(o)) consume();
                break;
            }
        }
        popped = expected;
    });

    const auto start = Clock::now();
    go.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    stop.store(true, std::memory_order_relaxed);

    t1.join();
    t2.join();

    const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
    return {popped, elapsed, ok && pushed == popped};
}

struct Summary {
    const char* name;
    double      median;  // objects per second
    double      best;
    double      worst;
    bool        ok;
};

template <typename Lock>
static Summary bench(const char* name, size_t capacity, double seconds, int runs) {
    std::printf("\n[%s]\n", name);

    std::vector<double> rates;
    bool all_ok = true;
    for (int r = 1; r <= runs; ++r) {
        const RunResult res  = run_once<Lock>(capacity, seconds);
        const double    rate = res.items / res.seconds;
        rates.push_back(rate);
        all_ok = all_ok && res.ok;

        std::printf("  run %d: %8.2f M objects/s  (%7.1f MB/s, %6.1f ns/object)  %s\n",
                    r, rate / 1e6, rate * sizeof(Order) / 1e6, 1e9 / rate,
                    res.ok ? "check ok" : "CHECK FAILED");
    }

    std::sort(rates.begin(), rates.end());
    const double median = rates[rates.size() / 2];
    std::printf("  median: %.2f M objects/s\n", median / 1e6);
    return {name, median, rates.back(), rates.front(), all_ok};
}

int main(int argc, char** argv) {
    const double seconds  = argc > 1 ? std::atof(argv[1]) : 1.0;
    const size_t capacity = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1024;
    const int    runs     = argc > 3 ? std::atoi(argv[3]) : 5;

    if (seconds <= 0 || capacity == 0 || runs <= 0) {
        std::fprintf(stderr, "usage: %s [seconds>0] [capacity>0] [runs>0]\n", argv[0]);
        return 1;
    }

    self_test<SpinLock>();
    self_test<std::mutex>();

    std::printf("SPSC queue with locks: %zu-byte objects, capacity %zu, %.2f s per run, %d runs\n",
                sizeof(Order), SPSCQueue<Order, SpinLock>(capacity).capacity(), seconds, runs);
    std::printf("self-test: SpinLock ok, std::mutex ok\n");

    const Summary results[] = {
        bench<SpinLock>("SpinLock", capacity, seconds, runs),
        bench<std::mutex>("std::mutex", capacity, seconds, runs),
    };

    std::printf("\n%-12s %14s %14s %14s %8s\n", "lock", "median (M/s)", "best (M/s)", "worst (M/s)", "check");
    for (const Summary& s : results) {
        std::printf("%-12s %14.2f %14.2f %14.2f %8s\n",
                    s.name, s.median / 1e6, s.best / 1e6, s.worst / 1e6, s.ok ? "ok" : "FAILED");
    }

    for (const Summary& s : results) {
        if (!s.ok) return 1;
    }
    return 0;
}
