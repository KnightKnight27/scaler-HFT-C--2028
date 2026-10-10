// tests for the queue
// compile: g++ -std=c++17 -pthread test.cpp -o test
// run:     ./test

#include <cassert>
#include <cstdio>
#include <mutex>
#include <thread>

#include "spsc_queue.hpp"

template <typename L>
void test_basic() {
    SPSCQueue<int, L> q(4);
    int x = 0;
    assert(q.capacity() == 4);
    assert(q.empty());
    assert(!q.pop(x));

    for (int i = 0; i < 4; ++i) assert(q.push(i));
    assert(q.full());
    assert(!q.push(99));

    assert(q.pop(x) && x == 0);
    assert(q.push(4));
    for (int i = 1; i <= 4; ++i) assert(q.pop(x) && x == i);
    assert(q.empty());
}

template <typename L>
void test_rounds_up_capacity() {
    SPSCQueue<int, L> q(5);
    assert(q.capacity() == 8);
}

template <typename L>
void test_wrap_around() {
    SPSCQueue<int, L> q(4);
    int x = 0, next = 0;
    for (int i = 0; i < 1000; ++i) {
        assert(q.push(2 * i));
        assert(q.push(2 * i + 1));
        assert(q.pop(x) && x == next++);
        assert(q.pop(x) && x == next++);
    }
    assert(q.empty());
}

template <typename L>
void test_batch() {
    SPSCQueue<int, L> q(8);
    int in[10], out[10];
    for (int i = 0; i < 10; ++i) in[i] = i;

    assert(q.push_n(in, 10) == 8); // only 8 fit
    assert(q.full());
    assert(q.pop_n(out, 3) == 3);
    assert(out[0] == 0 && out[1] == 1 && out[2] == 2);
    assert(q.push_n(in + 8, 2) == 2); // wraps around
    assert(q.pop_n(out, 10) == 7);
    for (int i = 0; i < 7; ++i) assert(out[i] == i + 3);
    assert(q.empty());
}

template <typename L>
void test_two_threads() {
    const int N = 300000;
    SPSCQueue<int, L> q(64);
    bool ok = true;

    std::thread t1([&] {
        for (int i = 0; i < N; ++i)
            while (!q.push(i)) {}
    });
    std::thread t2([&] {
        int x;
        for (int i = 0; i < N; ++i) {
            while (!q.pop(x)) {}
            if (x != i) ok = false;
        }
    });
    t1.join();
    t2.join();
    assert(ok);
    assert(q.empty());
}

template <typename L>
void run_all(const char* name) {
    test_basic<L>();
    test_rounds_up_capacity<L>();
    test_wrap_around<L>();
    test_batch<L>();
    test_two_threads<L>();
    std::printf("%s: all tests passed\n", name);
}

int main() {
    run_all<std::mutex>("std::mutex");
    run_all<SpinLock>("spinlock");
    run_all<TTASSpinLock>("ttas spinlock");
    return 0;
}
