// Lec-11 assignment : lock based SPSC queue + 1 second throughput test
// Kushal Talati (24BCS10123)
//
// build : make            (or)  g++ -std=c++20 -O2 -pthread spsc_queue.cpp -o spsc_queue
// run   : ./spsc_queue [trials] [capacity]

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>

// ---------------------------------------------------------------------------
// the thing we move around : exactly one cache line
// ---------------------------------------------------------------------------
struct Order {
    std::uint64_t id;
    std::int64_t  price;
    std::uint32_t qty;
    std::uint32_t side;
    std::uint64_t ts;
    char          pad[32];
};
static_assert(sizeof(Order) == 64, "Order has to be 64 bytes");

// ---------------------------------------------------------------------------
// lock #1 : a while loop on an atomic<bool>
// (i first tried test-and-test-and-set, i.e. spin on a relaxed load and only
//  then exchange, it was 2-3x SLOWER on the M3, see README)
// ---------------------------------------------------------------------------
struct SpinLock {
    std::atomic<bool> busy{false};

    void lock()   { while (busy.exchange(true, std::memory_order_acquire)) { } }
    void unlock() { busy.store(false, std::memory_order_release); }
};
// lock #2 is just std::mutex

// ---------------------------------------------------------------------------
// ring buffer guarded by whichever lock you give it
// the storage is a single heap block grabbed in the ctor (memory pool),
// after that push/pop never touch the allocator
// ---------------------------------------------------------------------------
template <class T, class Lock>
class LockedRing {
public:
    explicit LockedRing(std::size_t capacity)
        : cap_(capacity), pool_(new T[capacity]) {}

    bool push(const T& item) {
        std::lock_guard<Lock> g(lk_);
        if (count_ == cap_) return false;              // full, caller retries
        pool_[tail_] = item;
        if (++tail_ == cap_) tail_ = 0;
        ++count_;
        return true;
    }

    bool pop(T& out) {
        std::lock_guard<Lock> g(lk_);
        if (count_ == 0) return false;                 // empty, caller retries
        out = pool_[head_];
        if (++head_ == cap_) head_ = 0;
        --count_;
        return true;
    }

private:
    alignas(64) Lock lk_;                 // lock gets its own cache line
    alignas(64) std::size_t head_  = 0;   // next slot to pop
    std::size_t tail_  = 0;               // next slot to push
    std::size_t count_ = 0;               // items currently inside
    std::size_t cap_;
    std::unique_ptr<T[]> pool_;           // the pool, allocated exactly once
};

// ---------------------------------------------------------------------------
// benchmark : t1 produces, t2 consumes, main waits 1s then stops both
// ---------------------------------------------------------------------------
struct Result {
    std::uint64_t pushed = 0;
    std::uint64_t popped = 0;
    std::uint64_t checksum = 0;   // sum of ids seen by consumer, proves data really moved
};

template <class Lock>
Result run_once(std::size_t capacity) {
    LockedRing<Order, Lock> ring(capacity);
    alignas(64) std::atomic<bool> go{false};     // keep the flags away from
    alignas(64) std::atomic<bool> stop{false};   // anything that gets written
    Result r;

    // each thread counts into its own local and only writes to `r` once at the
    // end, otherwise pushed/popped share a cache line and ping-pong between cores
    std::thread t1([&] {                       // producer
        while (!go.load(std::memory_order_acquire)) { }
        std::uint64_t n = 0;
        Order o{};
        while (!stop.load(std::memory_order_relaxed)) {
            o.id    = n;
            o.price = 100000 + (std::int64_t)(n & 0xff);
            o.qty   = 1;
            if (ring.push(o)) ++n;
        }
        r.pushed = n;
    });

    std::thread t2([&] {                       // consumer
        while (!go.load(std::memory_order_acquire)) { }
        std::uint64_t n = 0, sum = 0;
        Order o;
        while (!stop.load(std::memory_order_relaxed)) {
            if (ring.pop(o)) { ++n; sum += o.id; }
        }
        r.popped = n;
        r.checksum = sum;
    });

    go.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop.store(true, std::memory_order_relaxed);

    t1.join();
    t2.join();

    // whatever t1 managed to push after t2 gave up is still sitting in the ring,
    // count it here so pushed == popped and the checksum is exact
    Order o;
    while (ring.pop(o)) { ++r.popped; r.checksum += o.id; }
    return r;
}

template <class Lock>
void bench(const char* label, int trials, std::size_t capacity) {
    std::printf("== %s  (capacity %zu, %d trials of 1s)\n", label, capacity, trials);
    std::printf("%-6s %16s %16s %10s\n", "trial", "pushed/s", "popped/s", "MB/s");
    std::uint64_t best = 0, total = 0;
    for (int i = 1; i <= trials; ++i) {
        Result r = run_once<Lock>(capacity);
        // every id 0..popped-1 must have arrived exactly once
        std::uint64_t expect = (r.popped == 0) ? 0 : (r.popped - 1) * r.popped / 2;
        if (r.checksum != expect) {
            std::printf("checksum mismatch in trial %d, queue is broken\n", i);
            std::exit(1);
        }
        std::printf("%-6d %16llu %16llu %10.1f\n", i,
                    (unsigned long long)r.pushed, (unsigned long long)r.popped,
                    r.popped * 64.0 / (1024.0 * 1024.0));
        total += r.popped;
        if (r.popped > best) best = r.popped;
    }
    std::printf("avg %llu objects/s   best %llu objects/s\n\n",
                (unsigned long long)(total / trials), (unsigned long long)best);
}

int main(int argc, char** argv) {
    int         trials   = argc > 1 ? std::atoi(argv[1]) : 5;
    std::size_t capacity = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 4096;

    bench<std::mutex>("std::mutex", trials, capacity);
    bench<SpinLock>("SpinLock (while loop)", trials, capacity);
    return 0;
}
