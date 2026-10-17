#include <iostream>
#include <thread>
#include <cassert>
#include "spsc_queue.hpp"

struct Order {
    uint64_t order_id;
    uint64_t timestamp_ns;
    double price;
    uint32_t quantity;
    char symbol[16];
    char side;
    char order_type;
    char padding[14];
};
static_assert(sizeof(Order) == 64, "Order struct must be exactly 64 bytes");

int main() {
    std::cout << "Testing SPSC Queue with Spinlock (Ring Buffer)...\n";
    SPSCQueueSpinLock<Order, 1024> queue;

    constexpr uint64_t kNumOrders = 100'000;

    std::thread producer([&]() {
        for (uint64_t i = 1; i <= kNumOrders; ++i) {
            Order order{};
            order.order_id = i;
            order.price = 100.5 + (i % 50);
            order.quantity = static_cast<uint32_t>(i % 10 + 1);
            order.side = (i % 2 == 0) ? 'B' : 'S';
            while (!queue.push(order)) {
                // busy wait until slot available
            }
        }
    });

    std::thread consumer([&]() {
        uint64_t received = 0;
        Order order{};
        while (received < kNumOrders) {
            if (queue.pop(order)) {
                ++received;
                assert(order.order_id == received);
            }
        }
    });

    producer.join();
    consumer.join();

    std::cout << "Successfully pushed and popped " << kNumOrders << " 64-byte orders!\n";
    return 0;
}
