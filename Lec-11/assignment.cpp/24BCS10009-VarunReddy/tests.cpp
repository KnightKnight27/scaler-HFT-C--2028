// Tests for SPSCQueue with both lock types. Build with -DSPSC_TSAN=ON to also
// let ThreadSanitizer check the two-thread test for data races.

#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

#include "spscqueue.h"

static int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::cout << "  FAILED line " << __LINE__ << ": " #cond "\n";        \
        }                                                                        \
    } while (0)

template <template <typename, size_t> class Queue>
void single_thread_tests() {
    {
        Queue<int, 4> q;
        int x = -1;
        CHECK(q.empty());
        CHECK(!q.full());
        CHECK(q.size() == 0);
        CHECK(!q.pop(x));  // pop on empty queue fails
        CHECK(x == -1);    // ...and leaves the output untouched
    }
    {
        Queue<int, 4> q;
        CHECK(q.push(1) && q.push(2) && q.push(3) && q.push(4));
        CHECK(q.full());
        CHECK(q.size() == 4);
        CHECK(!q.push(5));  // push on full queue fails
        int x;
        CHECK(q.pop(x) && x == 1);  // FIFO order
        CHECK(q.pop(x) && x == 2);
        CHECK(q.push(5));           // space freed by the pops is reused
        CHECK(q.pop(x) && x == 3);
        CHECK(q.pop(x) && x == 4);
        CHECK(q.pop(x) && x == 5);
        CHECK(q.empty());
    }
    {
        // Wrap around the ring buffer many times.
        Queue<int, 8> q;
        int next_in = 0, next_out = 0, x;
        bool ok = true;
        for (int round = 0; round < 1000; ++round) {
            for (int i = 0; i < 5; ++i) ok &= q.push(next_in++);
            for (int i = 0; i < 5; ++i) ok &= q.pop(x) && x == next_out++;
        }
        CHECK(ok);
        CHECK(q.empty());
    }
    {
        // Non-trivial element type (heap-allocated strings).
        Queue<std::string, 2> q;
        std::string s;
        CHECK(q.push(std::string(100, 'a')));
        CHECK(q.push("b"));
        CHECK(q.pop(s) && s == std::string(100, 'a'));
        CHECK(q.pop(s) && s == "b");
    }
}

// One producer thread and one consumer thread pass `n` numbers through a tiny
// queue, so it is constantly hitting both full and empty.
template <template <typename, size_t> class Queue>
void two_thread_test(uint64_t n) {
    Queue<uint64_t, 16> q;
    uint64_t received = 0, out_of_order = 0;

    std::thread t1([&] {
        for (uint64_t i = 0; i < n;) {
            if (q.push(i)) ++i;
        }
    });
    std::thread t2([&] {
        uint64_t x;
        while (received < n) {
            if (q.pop(x)) {
                out_of_order += (x != received);
                ++received;
            }
        }
    });
    t1.join();
    t2.join();

    CHECK(received == n);
    CHECK(out_of_order == 0);
    CHECK(q.empty());
}

int main() {
    const uint64_t n = 1000000;

    std::cout << "spinlock: single-thread tests\n";
    single_thread_tests<SpinQueue>();
    std::cout << "spinlock: producer/consumer test (" << n << " items)\n";
    two_thread_test<SpinQueue>(n);

    std::cout << "mutex: single-thread tests\n";
    single_thread_tests<MutexQueue>();
    std::cout << "mutex: producer/consumer test (" << n << " items)\n";
    two_thread_test<MutexQueue>(n);

    std::cout << (g_failures ? "\nFAILED: " + std::to_string(g_failures) + " check(s)\n" : "\nAll tests passed.\n");
    return g_failures ? 1 : 0;
}
