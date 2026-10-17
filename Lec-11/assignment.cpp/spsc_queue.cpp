#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

#include "spsc_queue.hpp"

struct alignas(64) Message {
    std::uint64_t data[8]{};
};

static_assert(sizeof(Message) == 64);

int main(){
    constexpr std::size_t queue_capacity = 2048;
    constexpr auto benchmark_time = std::chrono::seconds(1);

    SPSCQueue<Message, queue_capacity> queue;

    std::atomic<bool> running{true};

    std::uint64_t produced = 0;
    std::uint64_t consumed = 0;

    std::thread producer([&](){
        Message msg{};

        while (running.load(std::memory_order_relaxed)) {
            if (queue.push(msg))
                ++produced;
        }
    });

    std::thread consumer([&](){
        Message msg{};

        while (running.load(std::memory_order_relaxed)) {
            if (queue.pop(msg))
                ++consumed;
        }
    });

    std::this_thread::sleep_for(benchmark_time);

    running.store(false, std::memory_order_relaxed);

    producer.join();
    consumer.join();

    constexpr double bytes_per_object = sizeof(Message);

    const double mib_per_second =
        (static_cast<double>(consumed) * bytes_per_object) /
        (1024.0 * 1024.0);

    std::cout << "SPSC Queue Benchmark\n";
    std::cout << "--------------------\n";
    std::cout << "Object size:      " << sizeof(Message) << " bytes\n";
    std::cout << "Queue capacity:   " << queue_capacity << '\n';
    std::cout << "Produced/sec:     " << produced << '\n';
    std::cout << "Consumed/sec:     " << consumed << '\n';
    std::cout << "Throughput:       " << mib_per_second << " MiB/sec\n";

    return 0;
}