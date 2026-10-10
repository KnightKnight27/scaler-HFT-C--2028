#include <array>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <thread>

struct Order {
    long long id;
    char data[56];
};

template <std::size_t Capacity>
class SPSCQueue {
    static_assert(Capacity > 0, "Queue capacity must be greater than zero");

public:
    bool try_push(const Order& order) {
        std::lock_guard<std::mutex> lock(mutex);
        if (size == Capacity) {
            return false;
        }

        buffer[tail] = order;
        tail = (tail + 1) % Capacity;
        ++size;
        return true;
    }

    bool try_pop(Order& order) {
        std::lock_guard<std::mutex> lock(mutex);
        if (size == 0) {
            return false;
        }

        order = buffer[head];
        head = (head + 1) % Capacity;
        --size;
        return true;
    }

private:
    std::array<Order, Capacity> buffer{};
    std::size_t head = 0;
    std::size_t tail = 0;
    std::size_t size = 0;
    std::mutex mutex;
};

int main() {
    constexpr int order_count = 10000;
    SPSCQueue<1024> queue;
    bool sequence_is_valid = true;

    std::thread producer([&queue]() {
        for (int id = 0; id < order_count; ++id) {
            Order order{};
            order.id = id + 1;
            std::snprintf(order.data, sizeof(order.data), "order-%d", id);
            while (!queue.try_push(order)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&queue, &sequence_is_valid]() {
        for (int expected_id = 0; expected_id < order_count;) {
            Order order{};
            if (queue.try_pop(order)) {
                if (order.id != expected_id) {
                    sequence_is_valid = false;
                }
                ++expected_id;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    if (!sequence_is_valid) {
        std::cerr << "Queue returned orders out of sequence\n";
        return 1;
    }

    std::cot << "Processed " << order_count << " orders successfully\n";
    return 0;
}