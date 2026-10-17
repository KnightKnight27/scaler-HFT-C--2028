#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <cstdint>
#include <cstring>
#include <iomanip>

struct alignas(64) DataPacket {
    std::uint64_t sequence;
    std::uint64_t timestamp;
    unsigned char payload[48];

    explicit DataPacket(std::uint64_t number = 0)
        : sequence(number), timestamp(0) {
        std::memset(payload, 0, sizeof(payload));
    }
};

static_assert(sizeof(DataPacket) == 64, "Each packet must be 64 bytes");

class SPSCQueue {
public:
    explicit SPSCQueue(std::size_t capacity)
        : buffer(capacity), head(0), tail(0), count(0) {}

    bool try_push(const DataPacket& packet) {
        std::lock_guard<std::mutex> lock(mutex);

        if (count == buffer.size()) {
            return false; // The queue is full.
        }

        buffer[tail] = packet;
        tail = (tail + 1) % buffer.size();
        ++count;
        return true;
    }

    bool try_pop(DataPacket& packet) {
        std::lock_guard<std::mutex> lock(mutex);

        if (count == 0) {
            return false; // The queue is empty.
        }

        packet = buffer[head];
        head = (head + 1) % buffer.size();
        --count;
        return true;
    }

    bool empty() {
        std::lock_guard<std::mutex> lock(mutex);
        return count == 0;
    }

private:
    std::vector<DataPacket> buffer;
    std::size_t head;
    std::size_t tail;
    std::size_t count;
    std::mutex mutex;
};

int main() {
    constexpr std::size_t queue_capacity = 65536;
    constexpr int benchmark_seconds = 1;

    SPSCQueue queue(queue_capacity);
    std::atomic<bool> stop_producer{false};
    std::atomic<bool> producer_finished{false};
    std::uint64_t pushed = 0;
    std::uint64_t popped = 0;

    std::cout << "SPSC queue benchmark\n";
    std::cout << "Object size: " << sizeof(DataPacket) << " bytes\n";
    std::cout << "Lock: std::mutex\n";
    std::cout << "Benchmark duration: " << benchmark_seconds << " second(s)\n\n";

    const auto start_time = std::chrono::steady_clock::now();

    std::thread producer([&]() {
        std::uint64_t sequence = 0;

        while (!stop_producer.load()) {
            DataPacket packet(++sequence);

            if (queue.try_push(packet)) {
                ++pushed;
            } else {
                // The queue is full, so try again.
                std::this_thread::yield();
            }
        }

        producer_finished.store(true);
    });

    std::thread consumer([&]() {
        DataPacket packet;

        // Keep consuming until the producer has stopped and the queue is empty.
        while (!producer_finished.load() || !queue.empty()) {
            if (queue.try_pop(packet)) {
                ++popped;
            } else {
                std::this_thread::yield();
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(benchmark_seconds));
    stop_producer.store(true);

    producer.join();
    consumer.join();

    const auto end_time = std::chrono::steady_clock::now();
    const double elapsed_seconds =
        std::chrono::duration<double>(end_time - start_time).count();

    const double pushes_per_second = pushed / elapsed_seconds;
    const double pops_per_second = popped / elapsed_seconds;
    const double throughput_mb =
        (popped * sizeof(DataPacket)) / (elapsed_seconds * 1024.0 * 1024.0);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Actual elapsed time: " << elapsed_seconds << " seconds\n";
    std::cout << "Objects pushed: " << pushed << '\n';
    std::cout << "Objects popped: " << popped << '\n';
    std::cout << "Push operations per second: " << pushes_per_second << '\n';
    std::cout << "Pop operations per second: " << pops_per_second << '\n';
    std::cout << "Consumer throughput: " << throughput_mb << " MiB/s\n";

    return 0;
}
