#include <cstddef>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include "Memory_Pool.hpp"
#include "Spsc_Queue.cpp"

constexpr std::size_t kQueueCapacity = 2048;
constexpr std::size_t kPoolCapacity = kQueueCapacity * 2;

struct alignas(64) Message {
    std::uint64_t sequence;
    unsigned char payload[56]{};
};

static_assert(sizeof(Message) == 64);

void producer(SpscQueue<Message*, kQueueCapacity>& queue,
              MemoryPool<Message, kPoolCapacity>& pool,
              std::atomic<bool>& stop_requested,
              std::atomic<std::uint64_t>& produced) {
    std::uint64_t sequence = 0;
    while (!stop_requested.load(std::memory_order_relaxed)) {
        Message* message = pool.acquire();
        if (message == nullptr) {
            std::this_thread::yield();
            continue;
        }

        message->sequence = sequence++;
        while (!queue.push(message)) {
            std::this_thread::yield();
        }
        produced.fetch_add(1, std::memory_order_relaxed);
    }
}

void consumer(SpscQueue<Message*, kQueueCapacity>& queue,
              MemoryPool<Message, kPoolCapacity>& pool,
              std::atomic<bool>& producer_done,
              const std::atomic<std::uint64_t>& produced,
              std::atomic<std::uint64_t>& consumed,
              bool& values_match) {
    Message* message = nullptr;
    std::uint64_t expected_sequence = 0;
    while (!producer_done.load(std::memory_order_relaxed) ||
           consumed.load(std::memory_order_relaxed) <
               produced.load(std::memory_order_relaxed)) {
        if (!queue.pop(message)) {
            std::this_thread::yield();
            continue;
        }

        if (message->sequence != expected_sequence++) {
            values_match = false;
        }
        pool.release(message);
        consumed.fetch_add(1, std::memory_order_relaxed);
    }   
}

int main() {
    SpscQueue<Message*, kQueueCapacity> queue;
    MemoryPool<Message, kPoolCapacity> pool;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> producer_done{false};
    std::atomic<std::uint64_t> produced{0};
    std::atomic<std::uint64_t> consumed{0};
    bool values_match = true;

    const auto benchmark_start = std::chrono::steady_clock::now();
    std::thread producer_thread([&] {
        producer(queue, pool, stop_requested, produced);
        producer_done.store(true, std::memory_order_release);
    });
    std::thread consumer_thread(consumer, std::ref(queue), std::ref(pool),
                                std::ref(producer_done), std::cref(produced),
                                std::ref(consumed), std::ref(values_match));

    std::this_thread::sleep_for(std::chrono::seconds(1));
    const auto benchmark_end = std::chrono::steady_clock::now();
    stop_requested.store(true, std::memory_order_release);

    producer_thread.join();
    consumer_thread.join();

    const std::chrono::duration<double> measured_time =
        benchmark_end - benchmark_start;
    const double throughput = produced.load() / measured_time.count();

    std::cout << "Message size: " << sizeof(Message) << " bytes\n";
    std::cout << "Measured time: " << measured_time.count() << " seconds\n";
    std::cout << "Objects pushed: " << produced << '\n';
    std::cout << "Objects popped: " << consumed << '\n';
    std::cout << "Throughput: " << throughput << " objects/sec\n";
    std::cout << "Values in order: " << std::boolalpha << values_match << '\n';
    return values_match && produced == consumed ? 0 : 1;
}