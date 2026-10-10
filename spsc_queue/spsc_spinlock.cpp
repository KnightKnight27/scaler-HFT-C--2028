#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t kCapacity = 1024;  
constexpr std::chrono::seconds kDuration{1};

struct alignas(64) Object {
    std::uint64_t seq{0};
    std::uint8_t payload[56]{};
};
static_assert(sizeof(Object) == 64, "Object must be exactly 64 bytes");

class SpinLock {
public:
    void lock() noexcept {
        while (locked_.exchange(true, std::memory_order_acquire)) {
            while (locked_.load(std::memory_order_relaxed)) {}
        }
    }
    void unlock() noexcept { locked_.store(false, std::memory_order_release); }

private:
    std::atomic<bool> locked_{false};
};

class SpinlockQueue {
public:
    SpinlockQueue() : buffer_(kCapacity) {}

    bool try_push(const Object& item) {
        std::lock_guard<SpinLock> guard(lock_);
        if (count_ == kCapacity) return false;
        buffer_[tail_] = item;
        tail_ = (tail_ + 1) & (kCapacity - 1);
        ++count_;
        return true;
    }

    bool try_pop(Object& out) {
        std::lock_guard<SpinLock> guard(lock_);
        if (count_ == 0) return false;
        out = buffer_[head_];
        head_ = (head_ + 1) & (kCapacity - 1);
        --count_;
        return true;
    }

private:
    SpinLock lock_;
    std::vector<Object> buffer_;
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t count_{0};
};

}  
int main() {
    SpinlockQueue queue;
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
            if (queue.try_push(obj)) ++next;
        }
        pushed = next;
    });

    std::thread consumer([&] {
        while (!start.load(std::memory_order_acquire)) {}
        Object obj;
        std::uint64_t expected = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            if (queue.try_pop(obj)) {
                if (obj.seq != expected) in_order = false;
                ++expected;
            }
        }
        popped = expected;
    });

    const auto t0 = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(kDuration);
    stop.store(true, std::memory_order_relaxed);
    producer.join();
    consumer.join();
    const double secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::cout << "Implementation : spinlock\n"
              << "Object size    : " << sizeof(Object) << " bytes\n"
              << "Elapsed        : " << secs << " s\n"
              << "Pushed         : " << pushed << "\n"
              << "Popped         : " << popped << "\n"
              << "Throughput     : " << static_cast<std::uint64_t>(popped / secs)
              << " objects/sec\n"
              << "FIFO order     : " << (in_order ? "OK" : "VIOLATED") << "\n";
    return in_order ? 0 : 1;
}