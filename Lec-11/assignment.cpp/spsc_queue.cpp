// SPSC assignment: transfer 64-byte objects using std::mutex and measure objects/s.
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>

struct Message {
    std::array<std::uint64_t, 8> words{};
};
static_assert(sizeof(Message) == 64, "Messages must be exactly 64 bytes");

Message make_message(std::uint64_t sequence) {
    Message message;
    for (std::size_t i = 0; i < message.words.size(); ++i)
        message.words[i] = sequence + i;
    return message;
}

class SPSCQueue {
public:
    static constexpr std::size_t capacity = 4096;

    bool try_push(const Message& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == capacity)
            return false;
        slots_[write_] = message;
        write_ = (write_ + 1) % capacity;
        ++count_;
        return true;
    }

    bool try_pop(Message& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == 0)
            return false;
        message = slots_[read_];
        read_ = (read_ + 1) % capacity;
        --count_;
        return true;
    }

private:
    // Allocate all message storage once; reuse slots without per-message allocation.
    std::array<Message, capacity> slots_{};
    std::size_t read_ = 0;
    std::size_t write_ = 0;
    std::size_t count_ = 0;
    std::mutex mutex_;
};

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void test_queue() {
    SPSCQueue queue;
    Message message;
    require(!queue.try_pop(message), "Empty queue accepted a pop");

    // Shift the indices first, then fill/drain across the array boundary.
    require(queue.try_push(make_message(42)), "Initial push failed");
    require(queue.try_pop(message) && message.words == make_message(42).words,
            "Initial payload was corrupted");
    for (std::uint64_t round = 0; round < 3; ++round) {
        for (std::uint64_t i = 0; i < SPSCQueue::capacity; ++i)
            require(queue.try_push(make_message(round * SPSCQueue::capacity + i)),
                    "Queue became full too early");
        require(!queue.try_push(make_message(999)), "Full queue accepted a push");
        for (std::uint64_t i = 0; i < SPSCQueue::capacity; ++i) {
            require(queue.try_pop(message), "Queue became empty too early");
            require(message.words == make_message(round * SPSCQueue::capacity + i).words,
                    "FIFO order or payload was corrupted");
        }
        require(!queue.try_pop(message), "Drained queue accepted a pop");
    }

    constexpr std::uint64_t total = 1'000'000;
    bool valid = true; // Only the consumer writes this; main reads after join.
    std::thread producer([&] {
        for (std::uint64_t i = 0; i < total; ++i) {
            const Message next = make_message(i);
            while (!queue.try_push(next)) {}
        }
    });
    std::thread consumer([&] {
        for (std::uint64_t i = 0; i < total; ++i) {
            Message received;
            while (!queue.try_pop(received)) {}
            if (received.words != make_message(i).words)
                valid = false;
        }
    });
    producer.join();
    consumer.join();
    require(valid, "Concurrent transfer lost ordering or corrupted a payload");
    require(!queue.try_pop(message), "Concurrent transfer left extra messages");
    std::cout << "PASS: empty, full, wraparound, FIFO, and 1,000,000 complete 64-byte payloads\n";
}

void benchmark() {
    using Clock = std::chrono::steady_clock;
    SPSCQueue queue;
    std::atomic<unsigned> ready{0};
    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    Clock::time_point begin;
    std::uint64_t produced = 0;
    std::uint64_t consumed = 0;
    std::uint64_t checksum = 0;
    double seconds = 0;

    auto wait_for_start = [&] {
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {}
    };

    std::thread producer([&] {
        wait_for_start();
        Message message = make_message(0);
        while (!stop.load(std::memory_order_relaxed)) {
            if (queue.try_push(message)) {
                ++produced;
                message = make_message(produced);
            }
        }
    });
    std::thread consumer([&] {
        wait_for_start();
        const auto deadline = begin + std::chrono::seconds(1);
        Message message;
        while (Clock::now() < deadline) {
            if (queue.try_pop(message)) {
                ++consumed;
                // Observe every word, not just the sequence number.
                for (const auto word : message.words)
                    checksum += word;
            }
        }
        seconds = std::chrono::duration<double>(Clock::now() - begin).count();
        stop.store(true, std::memory_order_relaxed);
    });

    while (ready.load(std::memory_order_acquire) != 2) {}
    begin = Clock::now();
    start.store(true, std::memory_order_release);
    producer.join();
    consumer.join();

    require(produced >= consumed && produced - consumed <= SPSCQueue::capacity,
            "Invalid transfer counts");
    require(checksum == consumed * (4 * consumed + 24),
            "Payload checksum did not match the consumed sequence");
    std::cout << "Lock: std::mutex\n"
              << "Message bytes: " << sizeof(Message) << '\n'
              << "Queue capacity: " << SPSCQueue::capacity << '\n'
              << "Produced (including shutdown): " << produced << '\n'
              << "Consumed in measured interval: " << consumed << '\n'
              << "Remaining queued: " << produced - consumed << '\n'
              << std::fixed << std::setprecision(6)
              << "Elapsed seconds: " << seconds << '\n'
              << std::setprecision(0)
              << "Objects/second: " << consumed / seconds << '\n'
              << "Payload checksum: " << checksum << '\n';
}

int main(int argc, char** argv) {
    try {
        if (argc == 1)
            benchmark();
        else if (argc == 2 && std::string_view(argv[1]) == "--test")
            test_queue();
        else {
            std::cerr << "Usage: " << argv[0] << " [--test]\n";
            return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
