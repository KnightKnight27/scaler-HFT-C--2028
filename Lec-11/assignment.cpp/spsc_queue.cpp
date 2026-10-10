// Build: g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc_queue

#include "spsc_queue.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>

struct alignas(64) MarketEvent {
    std::uint64_t sequence{0};
    std::array<std::byte, 56> payload{};
};

static_assert(sizeof(MarketEvent) == 64);

template <typename LockPolicy>
void measure(const char* label) {
    constexpr std::size_t capacity = 4096;
    constexpr auto run_for = std::chrono::seconds(1);

    SPSCQueue<MarketEvent, LockPolicy> channel(capacity);
    std::atomic<bool> go{false};
    std::atomic<bool> producer_done{false};
    std::uint64_t sent = 0;
    std::uint64_t received = 0;
    bool fifo_ok = true;

    std::thread reader([&] {
        while (!go.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        MarketEvent event;
        std::uint64_t expected = 0;
        for (;;) {
            if (channel.try_pop(event)) {
                fifo_ok = fifo_ok && event.sequence == expected;
                ++expected;
                ++received;
            } else if (producer_done.load(std::memory_order_acquire) &&
                       channel.empty()) {
                break;
            } else {
                std::this_thread::yield();
            }
        }
    });

    std::thread writer([&] {
        while (!go.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        const auto end = std::chrono::steady_clock::now() + run_for;
        MarketEvent event;
        while (std::chrono::steady_clock::now() < end) {
            event.sequence = sent;
            if (channel.try_push(event)) {
                ++sent;
            } else {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    const auto started = std::chrono::steady_clock::now();
    go.store(true, std::memory_order_release);
    writer.join();
    reader.join();
    const double elapsed = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();

    const double mib = received * sizeof(MarketEvent) / (1024.0 * 1024.0);
    std::cout << label << '\n'
              << "  sent:       " << sent << '\n'
              << "  received:   " << received << '\n'
              << "  MiB/s:      " << std::fixed << std::setprecision(2)
              << mib << '\n'
              << "  FIFO check: " << (fifo_ok ? "PASS" : "FAIL") << '\n'
              << "  elapsed:    " << elapsed << " seconds\n";
}

int main() {
    std::cout << "64-byte SPSC transfer\n\n";
    measure<std::mutex>("mutex policy");
    std::cout << '\n';
    measure<BusyLock>("busy-lock policy");
}
