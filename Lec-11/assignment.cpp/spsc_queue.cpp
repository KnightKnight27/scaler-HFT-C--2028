
/*
email: ""
roll_no: ""
*/

// WRITE AN SPSC QUEUE
// Use std::mutex for synchronization.
// Producer and consumer threads with t1.join() and t2.join().
// Benchmark how many 64-byte objects can be pushed and popped per second.
// Add a README with per-second performance results.
// Raise a GitHub pull request.
// MEMORY POOL



#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

struct Message {
    std::uint64_t sequence;
    char payload[56];
};

static_assert(sizeof(Message) == 64, "Message must be 64 bytes");

constexpr std::size_t QUEUE_CAPACITY = 1024;
constexpr auto TEST_DURATION = std::chrono::seconds(1);

template <typename T, std::size_t Capacity>
class MutexSPSCQueue {
public:
    bool push(const T& value) {
        std::lock_guard<std::mutex> guard(mutex_);

        if (count_ == Capacity) {
            return false;
        }

        buffer_[tail_] = value;
        tail_ = (tail_ + 1) % Capacity;
        ++count_;
        return true;
    }

    bool pop(T& value) {
        std::lock_guard<std::mutex> guard(mutex_);

        if (count_ == 0) {
            return false;
        }

        value = buffer_[head_];
        head_ = (head_ + 1) % Capacity;
        --count_;
        return true;
    }

private:
    std::array<T, Capacity> buffer_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t count_ = 0;
    std::mutex mutex_;
};

int main() {
    MutexSPSCQueue<Message, QUEUE_CAPACITY> queue;

    std::atomic<bool> producer_done{false};
    std::uint64_t pushed = 0;
    std::uint64_t popped = 0;
    bool correct = true;

    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + TEST_DURATION;

    std::thread t1([&]() {
        while (std::chrono::steady_clock::now() < deadline) {
            Message message{};
            message.sequence = pushed;

            if (queue.push(message)) {
                ++pushed;
            } else {
                std::this_thread::yield();
            }
        }

        producer_done.store(true, std::memory_order_release);
    });

    std::thread t2([&]() {
        Message message{};

        while (true) {
            if (queue.pop(message)) {
                if (message.sequence != popped) {
                    correct = false;
                }
                ++popped;
            } else if (producer_done.load(std::memory_order_acquire)) {
                // Producer has stopped and the queue is empty.
                break;
            } else {
                std::this_thread::yield();
            }
        }
    });

    t1.join();
    t2.join();

    const auto finish = std::chrono::steady_clock::now();
    const double elapsed =
        std::chrono::duration<double>(finish - start).count();

    std::cout << "\nSPSC Queue - std::mutex\n"
              << "Message size: 64 bytes\n"
              << "Test duration: 1 second\n"
              << "Objects pushed in 1 second: " << pushed << '\n'
              << "Objects popped: " << popped << '\n'
              << "Total elapsed time (including drain): "
              << elapsed << " seconds\n"
              << "FIFO correctness: "
              << (correct && pushed == popped ? "PASS" : "FAIL")
              << '\n';

    return correct && pushed == popped ? 0 : 1;
}
