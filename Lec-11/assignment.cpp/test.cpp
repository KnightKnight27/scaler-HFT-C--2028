// basic tests for the queue
// compile: g++ -std=c++17 -pthread test.cpp -o test
// run:     ./test

#include <iostream>
#include <cassert>
#include <thread>
#include <mutex>

#include "spsc_queue.hpp"

using namespace std;

template <typename L>
void testEmptyAndFull() {
    SPSCQueue<int, L> q(3);
    int x;

    assert(q.empty());
    assert(!q.pop(x)); // nothing to pop

    assert(q.push(1));
    assert(q.push(2));
    assert(q.push(3));
    assert(q.full());
    assert(!q.push(4)); // should fail, queue is full

    assert(q.pop(x) && x == 1);
    assert(q.push(4)); // space again
    assert(q.size() == 3);
}

template <typename L>
void testWrapAround() {
    // small queue so head/tail go round many times
    SPSCQueue<int, L> q(4);
    int x;
    int next = 0;
    for (int i = 0; i < 100; i++) {
        assert(q.push(i * 2));
        assert(q.push(i * 2 + 1));
        assert(q.pop(x) && x == next++);
        assert(q.pop(x) && x == next++);
    }
    assert(q.empty());
}

template <typename L>
void testTwoThreads() {
    // producer pushes 0..N-1, consumer must get them in same order
    const int N = 200000;
    SPSCQueue<int, L> q(64);
    bool ok = true;

    thread t1([&]() {
        for (int i = 0; i < N; i++) {
            while (!q.push(i)) {}
        }
    });
    thread t2([&]() {
        int x;
        for (int i = 0; i < N; i++) {
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
void runAll(string name) {
    testEmptyAndFull<L>();
    testWrapAround<L>();
    testTwoThreads<L>();
    cout << name << " tests passed" << endl;
}

int main() {
    runAll<mutex>("std::mutex");
    runAll<SpinLock>("spinlock");
    cout << "all good" << endl;
    return 0;
}
