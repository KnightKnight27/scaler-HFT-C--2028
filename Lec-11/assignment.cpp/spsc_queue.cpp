// SPSC queue with locks
// one thread pushes, one thread pops
// we try std::mutex and a spinlock and see which one is faster

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

// the object we push around. exactly 64 bytes
struct Obj {
    uint64_t id;
    char pad[56];
};
static_assert(sizeof(Obj) == 64, "Obj must be 64 bytes");

// just loops until it gets the lock
class SpinLock {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

public:
    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
        }
    }
    void unlock() { flag.clear(std::memory_order_release); }
};

// the queue. it is a ring buffer
// buf is the memory pool: made once, never new/delete again
template <typename Lock, size_t N>
class Queue {
    Obj buf[N];
    size_t head = 0;   // where we pop from
    size_t tail = 0;   // where we push to
    size_t count = 0;  // how many items inside
    Lock lk;

public:
    bool push(const Obj& o) {
        std::lock_guard<Lock> g(lk);
        if (count == N) return false;  // full
        buf[tail] = o;
        tail = (tail + 1) % N;
        count++;
        return true;
    }

    bool pop(Obj& o) {
        std::lock_guard<Lock> g(lk);
        if (count == 0) return false;  // empty
        o = buf[head];
        head = (head + 1) % N;
        count--;
        return true;
    }
};

// runs the test for 1 second and returns how many objects were popped
template <typename Lock>
uint64_t run_test(const char* name) {
    static Queue<Lock, 1024> q;  // static so it does not sit on the stack

    std::atomic<bool> stop{false};
    uint64_t pushed = 0;
    uint64_t popped = 0;
    bool order_ok = true;

    
    std::thread t1([&]() {
        Obj o;
        std::memset(&o, 0, sizeof(o));
        uint64_t next = 0;
        while (!stop.load()) {
            o.id = next;
            if (q.push(o)) {
                next++;
            }
        }
        pushed = next;
    });

    // consumer
    std::thread t2([&]() {
        Obj o;
        uint64_t expected = 0;
        while (!stop.load()) {
            if (q.pop(o)) {
                // check items come out in the same order they went in
                if (o.id != expected) order_ok = false;
                expected++;
            }
        }
        popped = expected;
    });

    auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop.store(true);
    t1.join();
    t2.join();
    auto end = std::chrono::steady_clock::now();

    double secs = std::chrono::duration<double>(end - start).count();

    std::cout << name << "\n";
    std::cout << "  pushed: " << pushed << "\n";
    std::cout << "  popped: " << popped << "\n";
    std::cout << "  time:   " << secs << " s\n";
    std::cout << "  popped per second: " << (uint64_t)(popped / secs) << "\n";
    std::cout << "  order ok: " << (order_ok ? "yes" : "NO") << "\n\n";
    return popped;
}

int main() {
    std::cout << "object size = " << sizeof(Obj) << " bytes\n\n";
    run_test<std::mutex>("std::mutex");
    run_test<SpinLock>("spinlock");
    return 0;
}
