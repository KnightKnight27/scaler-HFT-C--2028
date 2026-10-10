#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace {
constexpr std::size_t kQueueCapacity = 1 << 18;
constexpr std::size_t kBenchmarkOps = 2'000'000;

struct alignas(64) Packet {
    std::uint64_t id;
    std::array<std::uint64_t, 7> payload{};
};
}

class SPSCQueue {
public:
    explicit SPSCQueue(std::size_t capacity)
        : buffer_(capacity), mcapacity(capacity) {}

    bool push(const Packet& item) {
        std::lock_guard<std::mutex> lock(mlock);

        if (is_full()) {
            return false;
        }

        buffer_[mtail] = item;
        mtail = (mtail + 1) % mcapacity;
        return true;
    }

    bool pop(Packet& out) {
        std::lock_guard<std::mutex> lock(mlock);

        if (is_empty()) {
            return false;
        }

        out = buffer_[mhead];
        mhead = (mhead + 1) % mcapacity;
        return true;
    }

private:
    bool is_full() const {
        return (mtail + 1) % mcapacity == mhead;
    }

    bool is_empty() const {
        return mhead == mtail;
    }

    std::vector<Packet> buffer_;
    alignas(64) std::size_t mhead = 0;
    alignas(64) std::size_t mtail = 0;
    std::size_t mcapacity = 0;
    std::mutex mlock;
};

static void run_benchmark() {
    SPSCQueue queue(kQueueCapacity);
    std::atomic<std::size_t> producer_done{0};
    std::atomic<std::size_t> consumer_seen{0};

    auto start = std::chrono::steady_clock::now();

    std::thread producer([&]() {
        for (std::size_t i = 0; i < kBenchmarkOps; ++i) {
            Packet item{};
            item.id = i;
            while (!queue.push(item)) {
                std::this_thread::yield();
            }
        }
        producer_done.store(1);
    });

    std::thread consumer([&]() {
        while (consumer_seen.load() < kBenchmarkOps) {
            Packet item{};
            if (queue.pop(item)) {
                consumer_seen.fetch_add(1);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    double ops_per_second = static_cast<double>(kBenchmarkOps) / elapsed;

    std::cout << "SPSC queue benchmark\n";
    std::cout << "Object size: " << sizeof(Packet) << " bytes\n";
    std::cout << "Operations: " << kBenchmarkOps << "\n";
    std::cout << "Elapsed: " << elapsed << " seconds\n";
    std::cout << "Throughput: " << ops_per_second << " 64-byte objects/sec\n";
}

int main() {
    run_benchmark();
    return 0;
}
