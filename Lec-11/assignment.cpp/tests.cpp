#include "spsc_queue.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

hft::Packet64 packet_for(std::uint64_t sequence) {
    hft::Packet64 packet;
    for (std::size_t i = 0; i < packet.words.size(); ++i) {
        packet.words[i] = sequence * 17 + i;
    }
    return packet;
}

template <typename Mutex, std::size_t Capacity>
void boundaries() {
    hft::BoundedSpscQueue<Capacity, Mutex> queue;
    auto output = packet_for(999);
    require(!queue.try_pop(output), "new queue was not empty");
    require(output.words == packet_for(999).words, "empty pop modified output");
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        for (std::size_t i = 0; i < Capacity; ++i) {
            require(queue.try_push(packet_for(i)), "pool filled too early");
        }
        require(!queue.try_push(packet_for(999)), "full queue accepted a packet");
        require(queue.try_pop(output), "pop failed");
        require(output.words == packet_for(0).words, "first packet damaged");
        // Recycle a slot while packets are still queued.
        require(queue.try_push(packet_for(Capacity)), "freed slot was not reusable");
        for (std::size_t i = 1; i <= Capacity; ++i) {
            require(queue.try_pop(output), "queued packet disappeared");
            require(output.words == packet_for(i).words, "FIFO/payload mismatch");
        }
        require(!queue.try_pop(output), "drained queue was not empty");
    }
}

template <typename Mutex, std::size_t Capacity>
void concurrent_delivery() {
    constexpr std::uint64_t count = 200000;
    hft::BoundedSpscQueue<Capacity, Mutex> queue;
    std::atomic<bool> failed{false};
    std::thread producer([&] {
        for (std::uint64_t i = 0; i < count; ++i) {
            const auto input = packet_for(i);
            while (!queue.try_push(input)) {
                if (failed.load(std::memory_order_relaxed)) return;
                std::this_thread::yield();
            }
        }
    });
    std::thread consumer;
    try {
        consumer = std::thread([&] {
            hft::Packet64 output;
            for (std::uint64_t i = 0; i < count; ++i) {
                while (!queue.try_pop(output)) std::this_thread::yield();
                if (output.words != packet_for(i).words) {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }
            }
        });
    } catch (...) {
        failed.store(true, std::memory_order_relaxed);
        producer.join();
        throw;
    }
    producer.join();
    consumer.join();
    require(!failed.load(), "concurrent FIFO/payload mismatch");
    hft::Packet64 output;
    require(!queue.try_pop(output), "concurrent test left packets behind");
}

template <typename Mutex>
void check_lock(const char* name) {
    boundaries<Mutex, 1>();
    boundaries<Mutex, 3>();
    concurrent_delivery<Mutex, 1>();
    concurrent_delivery<Mutex, 7>();
    std::cout << name << ": PASS (boundaries, recycling, 400000 concurrent packets)\n";
}
} // namespace

int main() {
    try {
        check_lock<std::mutex>("std::mutex");
        check_lock<hft::SpinMutex>("SpinMutex");
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
