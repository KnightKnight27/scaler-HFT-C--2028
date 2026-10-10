// SPSC Queue Assignment - HFT C++
// Student Details:
// Email: angel.24bcs10011@sst.scaler.com
// Roll No: 10011

#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstdint>

// 64-byte object as specified in assignment
struct alignas(64) Object64 {
    uint64_t data[8]; // 8 * 8 = 64 bytes

    Object64() {
        for (int i = 0; i < 8; ++i) data[i] = 0;
    }
    explicit Object64(uint64_t val) {
        data[0] = val;
        for (int i = 1; i < 8; ++i) data[i] = val + i;
    }
};

static_assert(sizeof(Object64) == 64, "Object64 must be exactly 64 bytes");

// Global sink to prevent compiler optimization
std::atomic<uint64_t> g_sink{0};

// ============================================================================
// Lec-10: Mutex-based SPSC Queue using power-of-2 circular buffer
// ============================================================================
template <typename T, size_t Capacity = 65536>
class MutexSPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static constexpr size_t kMask = Capacity - 1;

    std::vector<T> buffer_;
    size_t head_{0};
    size_t tail_{0};
    std::mutex mtx_;

public:
    MutexSPSCQueue() : buffer_(Capacity) {}

    bool push(const T& item) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (tail_ - head_ == Capacity) {
            return false; // full
        }
        buffer_[tail_ & kMask] = item;
        ++tail_;
        return true;
    }

    bool pop(T& item) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (head_ == tail_) {
            return false; // empty
        }
        item = buffer_[head_ & kMask];
        ++head_;
        return true;
    }
};

int main() {
    std::cout << "Lec-10: Testing basic Mutex SPSC Queue\n";
    MutexSPSCQueue<Object64> q;

    constexpr uint64_t N = 100000;
    std::thread t1([&]() {
        for (uint64_t i = 0; i < N; ++i) {
            while (!q.push(Object64(i))) {}
        }
    });

    std::thread t2([&]() {
        Object64 obj;
        uint64_t count = 0;
        while (count < N) {
            if (q.pop(obj)) {
                g_sink += obj.data[0];
                ++count;
            }
        }
    });

    t1.join();
    t2.join();

    std::cout << "Finished " << N << " operations successfully. Sink: " << g_sink.load() << "\n";
    return 0;
}
