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

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define CPU_PAUSE() _mm_pause()
#elif defined(__aarch64__) || defined(__arm64__)
#define CPU_PAUSE() asm volatile("yield")
#else
#define CPU_PAUSE() do {} while(0)
#endif

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

static_assert(sizeof(Object) == 64,  "Object must be exactly 64 bytes");
static_assert(alignof(Object) == 64, "Object must be 64-byte cache-line aligned");

class SpinLock {
private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

public:
    SpinLock() = default;
    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

    void lock() noexcept {
        while (flag.test_and_set(std::memory_order_acquire)) {
            CPU_PAUSE();
        }
    }

    void unlock() noexcept {
        flag.clear(std::memory_order_release);
    }
};

template <typename LockPolicy>
inline constexpr bool lock_is_noexcept =
    noexcept(std::declval<LockPolicy&>().lock());

template <typename T>
class MemoryPool {
public:
    explicit MemoryPool(size_t capacity)
        : slots_(capacity)
    {}

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    T& slot(size_t idx) noexcept             { return slots_[idx]; }
    const T& slot(size_t idx) const noexcept { return slots_[idx]; }

    size_t capacity() const noexcept { return slots_.size(); }

private:
    std::vector<T> slots_;
};

template <typename T, typename LockPolicy>
class SPSCQueue {
private:
    MemoryPool<T>   pool_;
    const size_t    capacity_;
    const size_t    mask_;
    size_t          head_{0};
    size_t          tail_{0};
    LockPolicy      lock_;

public:
    explicit SPSCQueue(size_t capacity)
        : pool_(capacity), capacity_(capacity), mask_(capacity - 1)
    {
        if (capacity == 0)
            throw std::invalid_argument("SPSCQueue: capacity must be > 0");
        if ((capacity & (capacity - 1)) != 0)
            throw std::invalid_argument("SPSCQueue: capacity must be a power of 2");
    }

    bool push(const T& item) noexcept(lock_is_noexcept<LockPolicy>) {
        std::lock_guard<LockPolicy> guard(lock_);
        if ((tail_ - head_) == capacity_) {
            return false;
        }
        pool_.slot(tail_ & mask_) = item;
        ++tail_;
        return true;
    }

    bool pop(T& item) noexcept(lock_is_noexcept<LockPolicy>) {
        std::lock_guard<LockPolicy> guard(lock_);
        if (tail_ == head_) {
            return false;
        }
        item = pool_.slot(head_ & mask_);
        ++head_;
        return true;
    }

    size_t size() noexcept(lock_is_noexcept<LockPolicy>) {
        std::lock_guard<LockPolicy> guard(lock_);
        return tail_ - head_;
    }

    size_t capacity() const noexcept {
        return capacity_;
    }
};

struct BenchResult {
    uint64_t pushed;
    uint64_t popped;
    uint64_t drained;
    double   elapsed_sec;
    bool     fifo_ok;
};

template <typename LockPolicy>
BenchResult run_benchmark(const std::string& lock_name, size_t capacity, double duration_sec = 1.0) {
    SPSCQueue<Object, LockPolicy> queue(capacity);

    std::atomic<bool>     start_flag{false};
    std::atomic<bool>     producer_done{false};
    std::atomic<uint64_t> pushed_count{0};
    std::atomic<uint64_t> total_pushed_count{0};
    std::atomic<uint64_t> popped_count{0};
    std::atomic<uint64_t> drained_count{0};
    std::atomic<bool>     order_valid{true};
    std::chrono::steady_clock::time_point deadline;

    std::thread t1([&]() {
        while (!start_flag.load(std::memory_order_acquire)) {
            CPU_PAUSE();
        }

        uint64_t seq = 0;
        Object obj;
        while (std::chrono::steady_clock::now() < deadline) {
            obj.sequence  = seq;
            obj.timestamp = seq * 2;

            if (queue.push(obj)) {
                ++seq;
                total_pushed_count.store(seq, std::memory_order_relaxed);
                if (std::chrono::steady_clock::now() <= deadline) {
                    pushed_count.fetch_add(1, std::memory_order_relaxed);
                }
            } else {
                if (std::chrono::steady_clock::now() >= deadline) break;
                CPU_PAUSE();
            }
        }
        total_pushed_count.store(seq, std::memory_order_release);
        producer_done.store(true, std::memory_order_release);
    });

    std::thread t2([&]() {
        while (!start_flag.load(std::memory_order_acquire)) {
            CPU_PAUSE();
        }

        uint64_t expected_seq = 0;
        uint64_t drained = 0;
        Object obj;

        while (std::chrono::steady_clock::now() < deadline) {
            if (queue.pop(obj)) {
                if (obj.sequence != expected_seq) {
                    order_valid.store(false, std::memory_order_relaxed);
                }
                ++expected_seq;
                if (std::chrono::steady_clock::now() <= deadline) {
                    popped_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    ++drained;
                }
            } else {
                CPU_PAUSE();
            }
        }

        while (true) {
            if (queue.pop(obj)) {
                if (obj.sequence != expected_seq) {
                    order_valid.store(false, std::memory_order_relaxed);
                }
                ++expected_seq;
                ++drained;
            } else if (producer_done.load(std::memory_order_acquire)) {
                break;
            } else {
                CPU_PAUSE();
            }
        }
        drained_count.store(drained, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    const auto t_start = std::chrono::steady_clock::now();
    deadline = t_start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(duration_sec));
    start_flag.store(true, std::memory_order_release);

    std::this_thread::sleep_until(deadline);

    t1.join();
    t2.join();

    const double   elapsed_sec  = std::chrono::duration<double>(deadline - t_start).count();
    const uint64_t pushed       = pushed_count.load(std::memory_order_acquire);
    const uint64_t popped       = popped_count.load(std::memory_order_acquire);
    const uint64_t total_pushed = total_pushed_count.load(std::memory_order_acquire);
    const uint64_t drained      = drained_count.load(std::memory_order_acquire);
    const bool     order_ok     = order_valid.load(std::memory_order_acquire);
    const bool     fifo_ok      = order_ok && ((popped + drained) == total_pushed);

    const double   push_ops     = static_cast<double>(pushed) / elapsed_sec;
    const double   pop_ops      = static_cast<double>(popped) / elapsed_sec;
    const double   mb_per_s     = (popped * sizeof(Object)) / (1024.0 * 1024.0 * elapsed_sec);

    std::cout << "    Trial"
              << " | Lock: "    << std::left  << std::setw(10) << lock_name
              << " | Cap: "     << std::right << std::setw(6)  << capacity
              << " | Pushed: "  << std::setw(10) << pushed
              << " | Popped: "  << std::setw(10) << popped
              << " | Drain: "   << std::setw(6)  << drained
              << " | Elapsed: " << std::fixed << std::setprecision(3) << elapsed_sec << "s"
              << " | "  << std::setprecision(2) << std::setw(7) << (push_ops / 1e6) << " Mpush/s"
              << "  "   << std::setw(7) << (pop_ops  / 1e6) << " Mpop/s"
              << "  "   << std::setprecision(1) << std::setw(8) << mb_per_s << " MB/s"
              << "  FIFO: " << (fifo_ok ? "OK" : "FAIL")
              << "\n";

    return BenchResult{pushed, popped, drained, elapsed_sec, fifo_ok};
}

template <typename LockPolicy>
void run_trials(const std::string& lock_name, size_t capacity,
                int trials = 3, double duration_sec = 1.0) {
    std::cout << "  [" << lock_name << ", cap=" << capacity << "]\n";
    double sum_push_mops = 0, sum_pop_mops = 0, sum_mb = 0;
    bool   all_fifo = true;
    for (int t = 0; t < trials; ++t) {
        BenchResult r = run_benchmark<LockPolicy>(lock_name, capacity, duration_sec);
        sum_push_mops += (static_cast<double>(r.pushed) / r.elapsed_sec) / 1e6;
        sum_pop_mops  += (static_cast<double>(r.popped) / r.elapsed_sec) / 1e6;
        sum_mb        += (r.popped * sizeof(Object)) / (1024.0 * 1024.0 * r.elapsed_sec);
        all_fifo      &= r.fifo_ok;
    }
    std::cout << std::fixed << std::setprecision(2)
              << "  => Avg over " << trials << " trials:"
              << "  Push " << (sum_push_mops / trials) << " Mpush/s"
              << "  Pop "  << (sum_pop_mops  / trials) << " Mpop/s"
              << "  "      << std::setprecision(1) << (sum_mb / trials) << " MB/s"
              << "  FIFO: " << (all_fifo ? "ALL PASSED" : "SOME FAILED")
              << "\n\n";
}

static bool check(bool condition, const char* label) {
    std::cout << "  " << label << ": " << (condition ? "PASSED" : "FAILED") << "\n";
    return condition;
}

void run_tests() {
    bool ok = true;
    std::cout << "[Tests] Running comprehensive queue validation...\n";

    {
        SPSCQueue<Object, SpinLock> q(4);
        Object out;
        ok &= check(q.pop(out) == false,  "T1: pop empty returns false");
        ok &= check(q.size()   == 0,      "T1: size is 0 when empty");
    }

    {
        SPSCQueue<Object, SpinLock> q(4);
        Object in(42), out;
        ok &= check(q.push(in)         == true,  "T2: push succeeds");
        ok &= check(q.size()           == 1,     "T2: size is 1 after push");
        ok &= check(q.pop(out)         == true,  "T2: pop succeeds");
        ok &= check(out.sequence       == 42,    "T2: popped sequence matches");
        ok &= check(q.size()           == 0,     "T2: size is 0 after pop");
    }

    {
        SPSCQueue<Object, SpinLock> q(4);
        for (uint64_t i = 0; i < 4; ++i) { Object o(i); q.push(o); }
        Object extra(99);
        ok &= check(q.push(extra) == false,   "T3: push on full queue returns false");
        ok &= check(q.size()      == 4,       "T3: size stays at capacity");
    }

    {
        SPSCQueue<Object, std::mutex> q(64);
        for (uint64_t i = 0; i < 64; ++i) { Object o(i); q.push(o); }
        bool fifo = true;
        for (uint64_t i = 0; i < 64; ++i) {
            Object out;
            q.pop(out);
            if (out.sequence != i) { fifo = false; break; }
        }
        ok &= check(fifo,             "T4: FIFO ordering over 64 objects");
        Object dummy;
        ok &= check(q.pop(dummy) == false, "T4: queue empty after full drain");
    }

    {
        SPSCQueue<Object, SpinLock> q(4);
        for (uint64_t i = 0; i < 4; ++i) { Object o(i); q.push(o); }
        Object tmp;
        q.pop(tmp); q.pop(tmp);
        Object a(10), b(11);
        q.push(a); q.push(b);
        uint64_t expected[] = {2, 3, 10, 11};
        bool wrap_ok = true;
        for (uint64_t e : expected) {
            Object out;
            q.pop(out);
            if (out.sequence != e) { wrap_ok = false; break; }
        }
        ok &= check(wrap_ok, "T5: ring-buffer wraparound FIFO");
    }

    {
        bool threw = false;
        try { SPSCQueue<Object, SpinLock> q(0); }
        catch (const std::invalid_argument&) { threw = true; }
        ok &= check(threw, "T6: zero capacity throws invalid_argument");
    }

    {
        bool threw = false;
        try { SPSCQueue<Object, SpinLock> q(3); }
        catch (const std::invalid_argument&) { threw = true; }
        ok &= check(threw, "T7: non-power-of-2 capacity throws invalid_argument");
    }

    std::cout << "\n[Tests] Overall result: " << (ok ? "ALL PASSED" : "SOME FAILED") << "\n\n";
    if (!ok) std::exit(1);
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--test") {
        run_tests();
        return 0;
    }

    std::cout << "===================================================================================================\n";
    std::cout << "   SPSC QUEUE BENCHMARK: 64-BYTE OBJECTS PUSH/POP IN 1 SECOND (3 trials each)\n";
    std::cout << "   Roll No: 24BCS10157 | Name: Sourabh Srivastva\n";
    std::cout << "   Object Size: " << sizeof(Object) << " bytes (aligned to " << alignof(Object) << " bytes)\n";
    std::cout << "===================================================================================================\n\n";

    constexpr int    TRIALS   = 3;
    constexpr double DURATION = 1.0;

    std::cout << "[Benchmark 1: Capacity = 1,024 slots]\n";
    run_trials<SpinLock>  ("SpinLock",   1024,  TRIALS, DURATION);
    run_trials<std::mutex>("std::mutex", 1024,  TRIALS, DURATION);

    std::cout << "[Benchmark 2: Capacity = 65,536 slots]\n";
    run_trials<SpinLock>  ("SpinLock",   65536, TRIALS, DURATION);
    run_trials<std::mutex>("std::mutex", 65536, TRIALS, DURATION);

    std::cout << "===================================================================================================\n";
    std::cout << "All benchmarks completed. Threads joined (t1.join, t2.join) after every trial.\n";
    return 0;
}
