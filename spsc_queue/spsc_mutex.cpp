#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace {


struct alignas(64) Object {
    std::uint64_t seq{0};
    std::uint8_t payload[56]{};
};
static_assert(sizeof(Object) == 64, "Object must be exactly 64 bytes");


template <typename T, std::size_t Capacity>
class MutexSpscQueue {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

public:
    MutexSpscQueue() : buffer_(Capacity) {}

    MutexSpscQueue(const MutexSpscQueue&) = delete;
    MutexSpscQueue& operator=(const MutexSpscQueue&) = delete;
    
    bool try_push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == Capacity) {
            return false;
        }
        buffer_[tail_] = item;
        tail_ = (tail_ + 1) & kMask;
        ++count_;
        return true;
    }
    
    bool try_pop(T& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == 0) {
            return false;
        }
        out = buffer_[head_];
        head_ = (head_ + 1) & kMask;
        --count_;
        return true;
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    std::mutex mutex_;
    std::vector<T> buffer_;
    std::size_t head_{0};   
    std::size_t tail_{0};  
    std::size_t count_{0};  
};

constexpr std::size_t kQueueCapacity = 1024;
constexpr std::chrono::seconds kBenchmarkDuration{1};

}  

int main() {
    MutexSpscQueue<Object, kQueueCapacity> queue;

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};

    std::uint64_t pushed = 0;
    std::uint64_t popped = 0;
    bool in_order = true;

    std::thread producer([&] {
        while (!start.load(std::memory_order_acquire)) {}
        Object obj;
        std::uint64_t next = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            obj.seq = next;
            if (queue.try_push(obj)) {
                ++next;
            }
        }
        pushed = next;
    });

    std::thread consumer([&] {
        while (!start.load(std::memory_order_acquire)) {}
        Object obj;
        std::uint64_t expected = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            if (queue.try_pop(obj)) {
                if (obj.seq != expected) {
                    in_order = false;
                }
                ++expected;
            }
        }
        popped = expected;
    });

    const auto t0 = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(kBenchmarkDuration);
    stop.store(true, std::memory_order_relaxed);
    producer.join();
    consumer.join();
    const auto t1 = std::chrono::steady_clock::now();

    const double seconds = std::chrono::duration<double>(t1 - t0).count();
    const double ops_per_sec = static_cast<double>(popped) / seconds;
    const double mb_per_sec = ops_per_sec * sizeof(Object) / (1024.0 * 1024.0);

    std::cout << "Implementation : std::mutex\n"
              << "Object size    : " << sizeof(Object) << " bytes\n"
              << "Queue capacity : " << kQueueCapacity << "\n"
              << "Elapsed        : " << seconds << " s\n"
              << "Pushed         : " << pushed << "\n"
              << "Popped         : " << popped << "\n"
              << "Throughput     : " << static_cast<std::uint64_t>(ops_per_sec)
              << " objects/sec (" << mb_per_sec << " MiB/s)\n"
              << "FIFO order     : " << (in_order ? "OK" : "VIOLATED") << "\n";

    return in_order ? 0 : 1;
}