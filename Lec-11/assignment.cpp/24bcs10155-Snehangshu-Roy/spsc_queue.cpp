// SPSC queue - Snehangshu Roy (24bcs10155)
//
// fixed size ring buffer, all slots allocated once up front (acts like a
// memory pool, no new/delete while running). one producer thread pushes
// 64 byte objects, one consumer thread pops them, we count how many get
// through in 1 second.
//
// two locks to compare:
//   SpinLock  -> atomic_flag + while loop
//   std::mutex
//
// build: g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

struct alignas(64) Msg {
    uint64_t seq;
    char payload[56];
};
static_assert(sizeof(Msg) == 64, "Msg should be 64 bytes");

class SpinLock {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

public:
    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
            // busy wait
        }
    }
    void unlock() { flag.clear(std::memory_order_release); }
};

template <typename T, typename Lock>
class SPSCQueue {
    std::vector<T> buf;  // the "pool", allocated once
    size_t cap;
    size_t head = 0;  // next pop
    size_t tail = 0;  // next push
    size_t count = 0;
    Lock lk;

public:
    explicit SPSCQueue(size_t capacity) : buf(capacity), cap(capacity) {}

    bool push(const T& item) {
        std::lock_guard<Lock> g(lk);
        if (count == cap) return false;  // full
        buf[tail] = item;
        tail = (tail + 1) % cap;
        count++;
        return true;
    }

    bool pop(T& out) {
        std::lock_guard<Lock> g(lk);
        if (count == 0) return false;  // empty
        out = buf[head];
        head = (head + 1) % cap;
        count--;
        return true;
    }
};

template <typename Lock>
void run_bench(const char* name, size_t capacity) {
    SPSCQueue<Msg, Lock> q(capacity);
    std::atomic<bool> stop{false};
    uint64_t pushed = 0, popped = 0;
    bool order_ok = true;

    std::thread t1([&] {  // producer
        Msg m;
        std::memset(m.payload, 'x', sizeof(m.payload));
        uint64_t seq = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            m.seq = seq;
            if (q.push(m)) seq++;
        }
        pushed = seq;
    });

    std::thread t2([&] {  // consumer
        Msg m;
        uint64_t expected = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            if (q.pop(m)) {
                if (m.seq != expected) order_ok = false;
                expected++;
            }
        }
        popped = expected;
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop = true;

    t1.join();
    t2.join();

    std::printf("%-10s cap=%-6zu pushed/s=%12llu  popped/s=%12llu  (%.1f MB/s)  order %s\n",
                name, capacity, (unsigned long long)pushed, (unsigned long long)popped,
                popped * sizeof(Msg) / (1024.0 * 1024.0), order_ok ? "ok" : "BROKEN");
}

int main() {
    std::printf("sizeof(Msg) = %zu bytes, run time = 1s each\n\n", sizeof(Msg));

    size_t caps[] = {64, 1024, 65536};
    for (size_t c : caps) {
        run_bench<SpinLock>("spinlock", c);
        run_bench<std::mutex>("mutex", c);
    }
    return 0;
}
