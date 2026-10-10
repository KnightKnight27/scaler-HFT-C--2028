// Build: g++ -std=c++17 -pthread test.cpp -o test

#include "spsc_queue.hpp"

#include <cassert>
#include <iostream>
#include <mutex>
#include <thread>

template <typename Queue>
void check_basic_fifo() {
    Queue queue(3);
    int value = 0;

    assert(queue.capacity() == 3);
    assert(queue.empty());
    assert(!queue.try_pop(value));

    assert(queue.try_push(11));
    assert(queue.try_push(22));
    assert(queue.try_push(33));
    assert(queue.full());
    assert(!queue.try_push(44));

    assert(queue.try_pop(value) && value == 11);
    assert(queue.try_pop(value) && value == 22);
    assert(queue.try_push(44));
    assert(queue.try_pop(value) && value == 33);
    assert(queue.try_pop(value) && value == 44);
    assert(queue.empty());
}

template <typename Queue>
void check_repeated_wraparound() {
    Queue queue(5);
    int value = 0;

    for (int round = 0; round < 250; ++round) {
        assert(queue.try_push(round));
        assert(queue.try_push(-round));
        assert(queue.try_pop(value) && value == round);
        assert(queue.try_pop(value) && value == -round);
    }
    assert(queue.size() == 0);
}

template <typename Queue>
void check_thread_handoff() {
    constexpr int count = 100000;
    Queue queue(128);
    bool sequence_is_valid = true;

    std::thread writer([&] {
        for (int value = 0; value < count; ++value) {
            while (!queue.try_push(value)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread reader([&] {
        for (int expected = 0; expected < count; ++expected) {
            int value = 0;
            while (!queue.try_pop(value)) {
                std::this_thread::yield();
            }
            if (value != expected) {
                sequence_is_valid = false;
            }
        }
    });

    writer.join();
    reader.join();
    assert(sequence_is_valid);
    assert(queue.empty());
}

template <typename LockPolicy>
void run_queue_checks(const char* label) {
    using Queue = SPSCQueue<int, LockPolicy>;
    check_basic_fifo<Queue>();
    check_repeated_wraparound<Queue>();
    check_thread_handoff<Queue>();
    std::cout << label << ": all checks passed\n";
}

int main() {
    run_queue_checks<std::mutex>("std::mutex");
    run_queue_checks<BusyLock>("busy-lock");
}
