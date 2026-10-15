
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

#include <array>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstdint>
#include <atomic>

struct Object {
    std::array<std::byte, 64> data{};
};

static_assert(sizeof(Object) == 64);

class SpinLock {
private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

public:
    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
            // Busy-wait until the lock becomes available
        }
    }

    void unlock() {
        flag.clear(std::memory_order_release);
    }
};

template <std::size_t Capacity>
class SPSCQueue {
private:
    std::array<Object, Capacity> buffer{};
    std::size_t head = 0;
    std::size_t tail = 0;
    std::size_t count = 0;
    std::mutex mtx;

public:
    bool push(const Object& obj) {
        std::lock_guard<std::mutex> lock(mtx);

        if (count == Capacity) {
            return false;
        }

        buffer[tail] = obj;
        tail = (tail + 1) % Capacity;
        ++count;
        return true;
    }

    bool pop(Object& obj) {
        std::lock_guard<std::mutex> lock(mtx);

        if (count == 0) {
            return false;
        }

        obj = buffer[head];
        head = (head + 1) % Capacity;
        --count;
        return true;
    }
};

int main() {
    constexpr std::size_t CAPACITY = 1024;
    constexpr std::uint64_t TOTAL = 5'000'000;

    SPSCQueue<CAPACITY> queue;
    Object obj{};

    std::uint64_t consumed = 0;
    std::atomic<bool> start_flag{false};

    std::thread t1([&]() {
	while (!start_flag.load(std::memory_order_acquire)) {
            // Wait for benchmark start
        }

        for (std::uint64_t i = 0; i < TOTAL;) {
            if (queue.push(obj)) {
                ++i;
            }
        }
    });

    std::thread t2([&]() {
	while (!start_flag.load(std::memory_order_acquire)) {
            // Wait for benchmark start
        }
        Object received{};

        while (consumed < TOTAL) {
            if (queue.pop(received)) {
                ++consumed;
            }
        }
    });
    auto start = std::chrono::steady_clock::now();
    start_flag.store(true, std::memory_order_release);

    t1.join();
    t2.join();

    auto end = std::chrono::steady_clock::now();
    double seconds =
        std::chrono::duration<double>(end - start).count();

    std::cout << "Object size: " << sizeof(Object) << " bytes\n";
    std::cout << "Objects consumed: " << consumed << '\n';
    std::cout << "Elapsed seconds: " << seconds << '\n';
    std::cout << "Completed objects/second: "
              << consumed / seconds << '\n';
}
