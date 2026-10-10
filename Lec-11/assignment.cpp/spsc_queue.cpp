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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert(Capacity > 0);

private:
    std::array<T, Capacity> buffer{};
    std::size_t head = 0;
    std::size_t tail = 0;
    std::size_t count = 0;
    std::mutex mutex;

public:
    bool push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex);

        if (count == Capacity) {
            return false;
        }

        buffer[tail] = item;
        tail = (tail + 1) % Capacity;
        ++count;

        return true;
    }

    bool pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex);

        if (count == 0) {
            return false;
        }

        item = buffer[head];
        head = (head + 1) % Capacity;
        --count;

        return true;
    }
};

struct Object {
    std::array<std::uint8_t, 64> data{};
};

static_assert(sizeof(Object) == 64);

int main() {
    constexpr std::uint64_t totalObjects = 5'000'000;
    constexpr std::size_t queueCapacity = 1024;

    SPSCQueue<Object, queueCapacity> queue;
    bool valid = true;

    auto start = std::chrono::steady_clock::now();

    std::thread producer([&]() {
        for (std::uint64_t i = 0; i < totalObjects; ++i) {
            Object item;
            std::memcpy(item.data.data(), &i, sizeof(i));

            while (!queue.push(item)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]() {
        for (std::uint64_t i = 0; i < totalObjects; ++i) {
            Object item;

            while (!queue.pop(item)) {
                std::this_thread::yield();
            }

            std::uint64_t received;
            std::memcpy(&received, item.data.data(), sizeof(received));

            if (received != i) {
                valid = false;
            }
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::steady_clock::now();

    double elapsedSeconds =
        std::chrono::duration<double>(end - start).count();

    double throughput = totalObjects / elapsedSeconds;

    std::cout << "Queue type: SPSC\n";
    std::cout << "Object size: " << sizeof(Object) << " bytes\n";
    std::cout << "Queue capacity: " << queueCapacity << '\n';
    std::cout << "Objects transferred: " << totalObjects << '\n';
    std::cout << "Data validation: "
              << (valid ? "PASSED" : "FAILED") << '\n';
    std::cout << "Elapsed time: " << elapsedSeconds << " seconds\n";
    std::cout << "Throughput: " << throughput << " objects/second\n";

    return valid ? 0 : 1;
}
