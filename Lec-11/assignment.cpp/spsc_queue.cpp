#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

struct alignas(64) Obj {
    uint64_t seq;
    char pad[56];
};
static_assert(sizeof(Obj) == 64, "Obj must be 64 bytes");

struct Lock {
    bool use_spin;
    std::mutex m;
    std::atomic_flag f = ATOMIC_FLAG_INIT;

    explicit Lock(bool spin) : use_spin(spin) {}

    void lock() {
        if (use_spin) {
            while (f.test_and_set(std::memory_order_acquire)) {}
        } else {
            m.lock();
        }
    }
    void unlock() {
        if (use_spin) f.clear(std::memory_order_release);
        else m.unlock();
    }
};

struct SpscQueue {
    SpscQueue(size_t cap, bool use_spin) : pool_(cap), lk_(use_spin) {}

    bool push(const Obj& o) {
        std::lock_guard<Lock> g(lk_);
        if (size_ == pool_.size()) return false;
        pool_[tail_] = o;
        tail_ = (tail_ + 1) % pool_.size();
        ++size_;
        return true;
    }

    bool pop(Obj& o) {
        std::lock_guard<Lock> g(lk_);
        if (size_ == 0) return false;
        o = pool_[head_];
        head_ = (head_ + 1) % pool_.size();
        --size_;
        return true;
    }

    std::vector<Obj> pool_;
    size_t head_ = 0, tail_ = 0, size_ = 0;
    Lock lk_;
};

struct Result {
    uint64_t pushed = 0, popped = 0, push_full = 0, pop_empty = 0;
    bool order_ok = true;
};

Result run_one_second(size_t cap, bool use_spin) {
    SpscQueue q(cap, use_spin);
    std::atomic<bool> stop{false};
    Result r;

    std::thread t1([&] {
        Obj o{};
        while (!stop.load(std::memory_order_relaxed)) {
            o.seq = r.pushed;
            if (q.push(o)) ++r.pushed;
            else ++r.push_full;
        }
    });

    std::thread t2([&] {
        Obj o;
        while (!stop.load(std::memory_order_relaxed)) {
            if (q.pop(o)) {
                if (o.seq != r.popped) r.order_ok = false;
                ++r.popped;
            } else {
                ++r.pop_empty;
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop.store(true, std::memory_order_relaxed);
    t1.join();
    t2.join();
    return r;
}

int main(int argc, char** argv) {
    size_t cap = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1024;
    if (cap == 0) return 2;

    Result m = run_one_second(cap, false);
    Result s = run_one_second(cap, true);
    if (!m.order_ok || !s.order_ok) {
        std::cerr << "order broken\n";
        return 1;
    }
    std::cout << "mutex " << m.popped << "\n";
    std::cout << "spinlock " << s.popped << "\n";
    return 0;
}
