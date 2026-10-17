#include "spsc_queue.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *description) {
    if (!condition) {
        throw std::runtime_error(description);
    }
}

template <class Lock> void test_queue() {
    bool rejected_zero = false;
    try {
        assignment::SPSCQueue<Lock> invalid(0);
    } catch (const std::invalid_argument &) {
        rejected_zero = true;
    }
    require(rejected_zero, "zero capacity was accepted");

    for (std::size_t capacity : {1U, 3U, 1024U}) {
        assignment::SPSCQueue<Lock> queue(capacity);
        assignment::Object out = assignment::make_object(99);
        require(!queue.try_pop(out), "empty pop succeeded");
        require(assignment::valid_object(out, 99), "failed pop changed output");
        for (unsigned cycle = 0; cycle < 20; ++cycle) {
            for (std::size_t i = 0; i < capacity; ++i) {
                require(queue.try_push(assignment::make_object(i)), "push failed before full");
            }
            require(!queue.try_push(assignment::make_object(999)), "full push succeeded");
            for (std::size_t i = 0; i < capacity; ++i) {
                require(queue.try_pop(out), "pop failed before empty");
                require(assignment::valid_object(out, i), "FIFO or payload mismatch");
            }
            require(!queue.try_pop(out), "drained queue is not empty");
        }
    }

    // Capacity one forces backpressure and slot reuse on every transfer.
    for (std::size_t capacity : {1U, 3U, 1024U}) {
        constexpr std::uint64_t count = 200000;
        assignment::SPSCQueue<Lock> queue(capacity);
        std::uint64_t errors = 0;
        std::thread producer([&] {
            for (std::uint64_t i = 0; i < count; ++i) {
                const auto object = assignment::make_object(i);
                while (!queue.try_push(object)) {
                    std::this_thread::yield();
                }
            }
        });
        std::thread consumer([&] {
            assignment::Object out;
            for (std::uint64_t i = 0; i < count; ++i) {
                while (!queue.try_pop(out)) {
                    std::this_thread::yield();
                }
                if (!assignment::valid_object(out, i)) {
                    ++errors;
                }
            }
        });
        producer.join();
        consumer.join();
        require(errors == 0, "concurrent sequence or payload mismatch");
        assignment::Object out;
        require(!queue.try_pop(out), "concurrent test left objects behind");
    }
}
} // namespace

int main() {
    try {
        test_queue<std::mutex>();
        test_queue<assignment::SpinLock>();
        std::cout << "PASS: mutex and spinlock; empty/full, FIFO, wraparound, 64-byte payloads, "
                     "1,200,000 concurrent transfers\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
