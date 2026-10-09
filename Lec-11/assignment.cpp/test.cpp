// SPSC Assignment - Rohan Ranjan - 10428
//
// small tests for the queue, runs each test with spinlock, std::mutex and lock-free
//
// build: g++ -std=c++17 -O2 -pthread test.cpp -o test
// run:   ./test

#undef NDEBUG // keep assert on even with -O2
#include <cassert>
#include <cstdio>
#include <mutex>
#include <thread>

#include "spsc_queue.hpp"

// empty queue gives nothing, full queue rejects push
template <typename Queue>
void test_empty_and_full() {
    Queue q(4);
    int x;
    assert(!q.pop(x));           // empty

    for (int i = 0; i < 4; i++) assert(q.push(i));
    assert(!q.push(99));         // full

    assert(q.pop(x) && x == 0);  // first in, first out
    assert(q.push(4));           // one slot free again
}

// small queue so head / tail go round the buffer many times
template <typename Queue>
void test_wrap_around() {
    Queue q(4);
    int x, next = 0;
    for (int i = 0; i < 1000; i++) {
        assert(q.push(2 * i));
        assert(q.push(2 * i + 1));
        assert(q.pop(x) && x == next++);
        assert(q.pop(x) && x == next++);
    }
    assert(!q.pop(x));           // empty at the end
}

// real producer + consumer threads, every number must come out in order
template <typename Queue>
void test_two_threads() {
    const int N = 1000000;
    Queue q(64);
    bool ok = true;

    std::thread t1([&] {
        for (int i = 0; i < N; i++)
            while (!q.push(i)) {}
    });
    std::thread t2([&] {
        int x;
        for (int i = 0; i < N; i++) {
            while (!q.pop(x)) {}
            if (x != i) ok = false;
        }
    });
    t1.join();
    t2.join();

    int x;
    assert(ok);
    assert(!q.pop(x));
}

template <typename Queue>
void run_all(const char* name) {
    test_empty_and_full<Queue>();
    test_wrap_around<Queue>();
    test_two_threads<Queue>();
    std::printf("%-22s tests passed\n", name);
}

int main() {
    run_all<LockedSPSCQueue<int, SpinLock>>("spinlock");
    run_all<LockedSPSCQueue<int, std::mutex>>("std::mutex");
    run_all<LockFreeSPSCQueue<int>>("lock-free (baseline)");
    std::printf("all good\n");
    return 0;
}
