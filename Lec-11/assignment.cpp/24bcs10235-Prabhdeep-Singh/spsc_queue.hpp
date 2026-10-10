#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
using namespace std;

// 64-byte object (one cache line)
struct Object {
    uint64_t data[8];
};
static_assert(sizeof(Object) == 64, "Object must be 64 bytes");

class SpinLock {
public:
    void lock() {
        while (flag_.test_and_set(memory_order_acquire)) {
        }
    }
    void unlock() { flag_.clear(memory_order_release); }

private:
    atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

// Fixed ring buffer allocated once (memory pool), guarded by Lock
template <size_t Capacity, typename Lock>
class SpscQueue {
public:
    bool push(const Object& obj) {
        lock_guard<Lock> lock(mu_);
        if (count_ == Capacity) return false;
        pool_[tail_] = obj;
        tail_ = (tail_ + 1) % Capacity;
        ++count_;
        return true;
    }

    bool pop(Object& out) {
        lock_guard<Lock> lock(mu_);
        if (count_ == 0) return false;
        out = pool_[head_];
        head_ = (head_ + 1) % Capacity;
        --count_;
        return true;
    }

private:
    array<Object, Capacity> pool_;
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;
    Lock mu_;
};
