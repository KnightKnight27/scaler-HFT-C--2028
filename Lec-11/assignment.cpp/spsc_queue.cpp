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

// SPSC queue with locks (spinlock or std::mutex)
// producer pushes 64 byte objects, consumer pops them, count how many in 1 second
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>

struct alignas(64) Obj { char data[64]; };
static_assert(sizeof(Obj) == 64, "obj must be 64 bytes");

struct SpinLock {
    std::atomic_flag f = ATOMIC_FLAG_INIT;
    void lock() { while (f.test_and_set(std::memory_order_acquire)) {} } // the while loop
    void unlock() { f.clear(std::memory_order_release); }
};

template <typename Lock, size_t N = 1024>
struct SPSCQueue {
    Obj buf[N]; // memory pool: preallocated slots, no new/delete in hot path
    size_t head = 0, tail = 0;
    Lock lk;

    bool push(const Obj& o) {
        std::lock_guard<Lock> g(lk);
        if (tail - head == N) return false; // full
        buf[tail % N] = o;
        tail++;
        return true;
    }
    bool pop(Obj& o) {
        std::lock_guard<Lock> g(lk);
        if (tail == head) return false; // empty
        o = buf[head % N];
        head++;
        return true;
    }
};

template <typename Lock>
void bench(const char* name) {
    auto qp = std::make_unique<SPSCQueue<Lock>>(); auto& q = *qp;
    std::atomic<bool> stop{false};
    long long pushed = 0, popped = 0;

    std::thread t1([&] {
        Obj o{};
        while (!stop.load(std::memory_order_relaxed)) {
            o.data[0]++;
            if (q.push(o)) pushed++;
        }
    });
    std::thread t2([&] {
        Obj o;
        while (!stop.load(std::memory_order_relaxed)) {
            if (q.pop(o)) popped++;
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop = true;
    t1.join();
    t2.join();

    printf("%-10s pushed: %12lld  popped: %12lld  (%.2f M ops/sec popped, %.2f MB/s)\n",
           name, pushed, popped, popped / 1e6, popped * 64.0 / (1024 * 1024));
}

int main() {
    for (int i = 0; i < 3; i++) {
        bench<SpinLock>("spinlock");
        bench<std::mutex>("mutex");
    }
}
