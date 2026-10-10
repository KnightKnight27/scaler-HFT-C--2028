#include <iostream>
#include "spscqueue.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <thread>
#include <string>
struct Event {
    std::uint64_t id = 0;
    char data[56]{};
};

static_assert(sizeof(Event) == 64);

int main(int argc, char* argv[]) {
    const int milliseconds = argc > 1 ? std::stoi(argv[1]) : 1000;

    SPSCQueue<Event, 1024> queue;
    std::atomic<int> ready{0};
    std::atomic<bool> started{false};
    std::atomic<bool> running{true};
    std::uint64_t pushes = 0;
    std::uint64_t pops = 0;
    std::uint64_t checksum = 0;

    std::thread producer([&] {
        Event event{};
        ready.fetch_add(1);
        while (!started.load(std::memory_order_acquire)) {}

        while (running.load(std::memory_order_relaxed)) {
            event.id = pushes + 1;
            if (queue.push(event)) {
                ++pushes;
            }
        }
    });

    std::thread consumer([&] {
        Event event{};
        ready.fetch_add(1);
        while (!started.load(std::memory_order_acquire)) {}

        while (running.load(std::memory_order_relaxed)) {
            if (queue.pop(event)) {
                ++pops;
                checksum += event.id;
            }
        }
    });

    while (ready.load() != 2) {}

    const auto begin = std::chrono::steady_clock::now();
    const auto deadline = begin + std::chrono::milliseconds(milliseconds);
    started.store(true, std::memory_order_release);
    std::this_thread::sleep_until(deadline);
    const auto end = std::chrono::steady_clock::now();
    running.store(false, std::memory_order_relaxed);

    producer.join();
    consumer.join();

    const double seconds = std::chrono::duration<double>(end - begin).count();
    const std::uint64_t total = pushes + pops;

    std::cout << "Event size: " << sizeof(Event) << " bytes\n"
              << "Run time: " << milliseconds << " ms\n"
              << "Push operations: " << pushes << '\n'
              << "Pop operations: " << pops << '\n'
              << "Total operations: " << total << '\n';

    std::cout << std::fixed << std::setprecision(0)
              << "Pushes per second: " << pushes / seconds << '\n'
              << "Pops per second: " << pops / seconds << '\n'
              << "Total operations per second: " << total / seconds << '\n'
              << "Checksum: " << checksum << '\n';
}
