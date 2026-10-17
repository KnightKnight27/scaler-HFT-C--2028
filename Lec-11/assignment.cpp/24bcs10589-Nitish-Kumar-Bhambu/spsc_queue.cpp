// spsc queue assignment - lec 11
// nitish kumar bhambu | 24bcs10589
//
// uses spinlock (while loop) and std::mutex
// measures how many 64 byte objects we can push+pop per second

#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstring>
#include <cstdint>

// 64 byte object (one cache line)
struct alignas(64) Payload {
    uint64_t id;
    uint64_t padding[7];
};
static_assert(sizeof(Payload) == 64, "Payload should be 64 bytes");

// simple spinlock with atomic_flag
struct SpinLock {
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;

    void lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // spin
        }
    }
    void unlock() {
        flag_.clear(std::memory_order_release);
    }
};

// SPSC queue using spinlock
// ring buffer acts as memory pool so no malloc/free in hot path
template<size_t Cap>
class SpinlockQueue {
    Payload buf_[Cap];
    size_t head_ = 0;
    size_t tail_ = 0;
    SpinLock lk_;
public:
    bool push(const Payload& p) {
        lk_.lock();
        if ((head_ - tail_) == Cap) {
            lk_.unlock();
            return false; // full
        }
        buf_[head_ % Cap] = p;
        head_++;
        lk_.unlock();
        return true;
    }

    bool pop(Payload& p) {
        lk_.lock();
        if (head_ == tail_) {
            lk_.unlock();
            return false; // empty
        }
        p = buf_[tail_ % Cap];
        tail_++;
        lk_.unlock();
        return true;
    }
};

// SPSC queue using std::mutex
template<size_t Cap>
class MutexQueue {
    Payload buf_[Cap];
    size_t head_ = 0;
    size_t tail_ = 0;
    std::mutex mtx_;
public:
    bool push(const Payload& p) {
        std::lock_guard<std::mutex> g(mtx_);
        if ((head_ - tail_) == Cap)
            return false;
        buf_[head_ % Cap] = p;
        head_++;
        return true;
    }

    bool pop(Payload& p) {
        std::lock_guard<std::mutex> g(mtx_);
        if (head_ == tail_)
            return false;
        p = buf_[tail_ % Cap];
        tail_++;
        return true;
    }
};

// benchmark a queue type for 1 second
template<typename Q>
void bench(const char* label) {
    Q* q = new Q(); // heap because its big
    std::atomic<bool> stop{false};
    uint64_t pushed_count = 0;
    uint64_t popped_count = 0;

    // producer thread
    std::thread t1([&]() {
        Payload p{};
        uint64_t seq = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            p.id = seq;
            if (q->push(p))
                seq++;
        }
        pushed_count = seq;
    });

    // consumer thread
    std::thread t2([&]() {
        Payload p{};
        uint64_t cnt = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            if (q->pop(p))
                cnt++;
        }
        // drain remaining items
        while (q->pop(p))
            cnt++;
        popped_count = cnt;
    });

    // let it run for 1 sec
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop.store(true);

    t1.join();
    t2.join();

    double million = popped_count / 1e6;
    double mbps = (popped_count * sizeof(Payload)) / (1024.0 * 1024.0);

    std::cout << label << ":\n";
    std::cout << "  pushed:     " << pushed_count << "\n";
    std::cout << "  popped:     " << popped_count << "\n";
    std::cout << "  throughput: " << million << " M ops/sec\n";
    std::cout << "  bandwidth:  " << mbps << " MB/sec\n";
    std::cout << "\n";

    delete q;
}

int main() {
    constexpr size_t CAPACITY = 1024;

    std::cout << "=== SPSC Queue Benchmark ===\n";
    std::cout << "Object size: " << sizeof(Payload) << " bytes\n";
    std::cout << "Queue capacity: " << CAPACITY << " slots\n";
    std::cout << "Duration: 1 second per test\n\n";

    bench<SpinlockQueue<CAPACITY>>("Spinlock (atomic_flag)");
    bench<MutexQueue<CAPACITY>>("std::mutex");

    return 0;
}
