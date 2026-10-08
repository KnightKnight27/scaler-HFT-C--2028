#include "../object64.hpp"
#include "../spsc_queue.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

int checks = 0;

void check(bool condition, const std::string& description) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(description);
    }
}

template <typename Lock>
void sequential_tests(const std::string& name) {
    bool rejected_zero = false;
    try {
        SPSCQueue<int, Lock> invalid(0);
    } catch (const std::invalid_argument&) {
        rejected_zero = true;
    }
    check(rejected_zero, name + ": zero capacity rejected");

    for (const std::size_t capacity : {1u, 7u, 1024u}) {
        SPSCQueue<Object64, Lock> queue(capacity);
        check(queue.capacity() == capacity, name + ": exact capacity");
        Object64 out = make_object(999);
        check(!queue.try_pop(out), name + ": initially empty");
        check(valid_object(out, 999), name + ": failed pop leaves output unchanged");

        // Repeatedly reuse every slot, including capacity 1 and a non-power of 2.
        for (std::uint64_t round = 0; round < 25; ++round) {
            for (std::size_t i = 0; i < capacity; ++i) {
                check(queue.try_push(make_object(round * capacity + i)), name + ": fill");
            }
            check(!queue.try_push(make_object(999999)), name + ": reject full push");
            for (std::size_t i = 0; i < capacity; ++i) {
                check(queue.try_pop(out), name + ": drain");
                check(valid_object(out, round * capacity + i), name + ": FIFO and all 64 bytes");
            }
            check(!queue.try_pop(out), name + ": empty after drain");
        }

        // Alternate pops and pushes without emptying the ring between operations.
        for (std::size_t i = 0; i < capacity; ++i) {
            check(queue.try_push(make_object(i)), name + ": mixed initial fill");
        }
        for (std::uint64_t i = 0; i < capacity * 10; ++i) {
            check(queue.try_pop(out) && valid_object(out, i), name + ": mixed FIFO");
            check(queue.try_push(make_object(capacity + i)), name + ": mixed push");
        }
        for (std::size_t i = 0; i < capacity; ++i) {
            check(queue.try_pop(out) && valid_object(out, capacity * 10 + i), name + ": mixed drain");
        }
    }

    // Scope exit must also clean up a partially filled queue.
    SPSCQueue<int, Lock> partial(3);
    check(partial.try_push(42), name + ": partially filled destruction");
}

template <typename Lock>
void concurrent_test(const std::string& name, std::size_t capacity, int slow_side) {
    constexpr std::uint64_t count = 250000;
    SPSCQueue<Object64, Lock> queue(capacity);
    std::atomic<bool> cancel{false};
    std::atomic<bool> producer_finished{false};
    std::atomic<bool> consumer_finished{false};
    std::uint64_t produced = 0;
    std::uint64_t consumed = 0;
    std::uint64_t errors = 0;

    std::thread producer([&] {
        while (produced < count && !cancel.load(std::memory_order_relaxed)) {
            if (queue.try_push(make_object(produced))) {
                ++produced;
                if (slow_side == 1 && produced % 127 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
            } else {
                std::this_thread::yield();
            }
        }
        producer_finished.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        Object64 out;
        while (consumed < count && !cancel.load(std::memory_order_relaxed)) {
            if (queue.try_pop(out)) {
                if (!valid_object(out, consumed)) {
                    ++errors;
                }
                ++consumed;
                if (slow_side == 2 && consumed % 127 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
            } else {
                std::this_thread::yield();
            }
        }
        consumer_finished.store(true, std::memory_order_release);
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    bool timed_out = false;
    while (!producer_finished.load(std::memory_order_acquire) ||
           !consumer_finished.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            timed_out = true;
            cancel.store(true, std::memory_order_relaxed);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    producer.join();
    consumer.join();

    check(!timed_out, name + ": concurrent transfer completed before watchdog");
    check(produced == count && consumed == count, name + ": no lost or duplicated objects");
    check(errors == 0, name + ": concurrent FIFO and complete payload integrity");
    Object64 out;
    check(!queue.try_pop(out), name + ": concurrent final queue empty");
    std::cout << "PASS " << name << " capacity=" << capacity << " slow_side=" << slow_side
              << " transferred=" << count << '\n';
}

template <typename Lock>
void test_lock(const std::string& name) {
    sequential_tests<Lock>(name);
    for (const std::size_t capacity : {1u, 7u, 1024u}) {
        for (int slow_side = 0; slow_side <= 2; ++slow_side) {
            concurrent_test<Lock>(name, capacity, slow_side);
        }
    }
}

int main() {
    static_assert(!std::is_copy_constructible<SPSCQueue<int>>::value, "Queue cannot be copied");
    static_assert(!std::is_move_constructible<SPSCQueue<int>>::value, "Queue cannot be moved");
    try {
        test_lock<std::mutex>("mutex");
        test_lock<SpinLock>("spin");
        std::cout << "PASS " << checks << " checks; 18 concurrent trials; 4500000 objects\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
