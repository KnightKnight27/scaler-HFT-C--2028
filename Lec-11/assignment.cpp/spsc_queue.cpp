// spsc queue, 64 byte objs. mutex vs spinlock vs lockfree, 1 sec each
// g++ -std=c++17 -O3 -pthread spsc_queue.cpp -o spsc_queue

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

using namespace std;



struct alignas(64) Obj { uint64_t seq; char pad[56]; };
static_assert(sizeof(Obj) == 64);

struct MutexLock { mutex m; void lock() { m.lock(); } void unlock() { m.unlock(); } };

// dumb spinlock, no backoff
struct SpinLock {
    atomic_flag f = ATOMIC_FLAG_INIT;
    void lock() { while (f.test_and_set(memory_order_acquire)); }
    void unlock() { f.clear(memory_order_release); }
};

// ring buffer allocated once = the "pool", no malloc while running
template <typename L, size_t N = 1024>
struct LockedQ {
    vector<Obj> buf = vector<Obj>(N);
    size_t head = 0, tail = 0, size = 0;
    L lk;

    bool push(const Obj& o) {
        lk.lock();
        if (size == N) { lk.unlock(); return false; }
        buf[tail] = o; tail = (tail + 1) % N; size++;
        lk.unlock();
        return true;
    }
    bool pop(Obj& o) {
        lk.lock();
        if (!size) { lk.unlock(); return false; }
        o = buf[head]; head = (head + 1) % N; size--;
        lk.unlock();
        return true;
    }
};

// only ok bc exactly 1 producer + 1 consumer. each side owns its own index.
// separate cache lines so they dont false share
template <size_t N = 1024>
struct LockFreeQ {
    vector<Obj> buf = vector<Obj>(N);
    alignas(64) atomic<size_t> head{0};
    alignas(64) atomic<size_t> tail{0};

    bool push(const Obj& o) {
        size_t t = tail.load(memory_order_relaxed);
        if (t - head.load(memory_order_acquire) == N) return false;
        buf[t & (N - 1)] = o;
        tail.store(t + 1, memory_order_release);
        return true;
    }
    bool pop(Obj& o) {
        size_t h = head.load(memory_order_relaxed);
        if (h == tail.load(memory_order_acquire)) return false;
        o = buf[h & (N - 1)];
        head.store(h + 1, memory_order_release);
        return true;
    }
};

template <typename Q>
void bench(const char* name) {
    Q q;
    atomic<bool> go{false}, stop{false}, done{false};
    uint64_t pushed = 0, popped = 0;
    bool ok = true;

    thread t1([&] {
        while (!go) {}
        Obj o{};
        uint64_t n = 0;
        while (!stop) { o.seq = n; if (q.push(o)) n++; }
        pushed = n;
        done = true;   // consumer has to wait for this or it misses the last push
    });

    thread t2([&] {
        while (!go) {}
        Obj o;
        uint64_t n = 0;
        for (;;) {
            if (q.pop(o)) { if (o.seq != n++) ok = false; }
            else if (done) {
                if (!q.pop(o)) break;
                if (o.seq != n++) ok = false;
            }
        }
        popped = n;
    });

    auto t0 = chrono::steady_clock::now();
    go = true;
    this_thread::sleep_for(chrono::seconds(1));
    stop = true;
    t1.join(); t2.join();
    double s = chrono::duration<double>(chrono::steady_clock::now() - t0).count();

    printf("%-18s pushed=%12llu popped=%12llu  %8.2f M ops/s  %6.2f GB/s  ordered=%s\n",
           name, (unsigned long long)pushed, (unsigned long long)popped,
           popped / s / 1e6, popped * 64 / s / 1e9, ok && pushed == popped ? "OK" : "FAIL");
}



int main() {
    printf("sizeof(Obj) = %zu bytes, hardware threads = %u\n\n", sizeof(Obj), thread::hardware_concurrency());
    bench<LockedQ<MutexLock>>("std::mutex");
    bench<LockedQ<SpinLock>>("spinlock");
    bench<LockFreeQ<>>("lock-free (extra)");
}
