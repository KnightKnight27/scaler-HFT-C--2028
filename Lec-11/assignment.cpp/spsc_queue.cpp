#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

#include "spsc_queue.hpp"

struct alignas(64) Payload {
    std::uint64_t words[8]{};
};

static_assert(sizeof(Payload) == 64);

int main() {
    constexpr auto duration = std::chrono::seconds(1);
    SPSCQueue<Payload, 1024> queue;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> pushes{0};
    std::atomic<std::uint64_t> pops{0};

    std::thread producer([&] {
        Payload payload;
        while (!stop.load(std::memory_order_relaxed))
            if (queue.push(payload))
                ++pushes;
    });

    std::thread consumer([&] {
        Payload payload;
        while (!stop.load(std::memory_order_relaxed))
            if (queue.pop(payload))
                ++pops;
    });

    std::this_thread::sleep_for(duration);
    stop.store(true, std::memory_order_relaxed);
    producer.join();
    consumer.join();

    std::cout << "pushes/sec: " << pushes.load() << '\n'
              << "pops/sec:   " << pops.load() << '\n';
}
