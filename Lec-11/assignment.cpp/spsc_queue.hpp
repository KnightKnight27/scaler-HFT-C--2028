#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <thread>

#include "item_pool.hpp"

class MutexLock {
public:
    void lock() { mutex_.lock(); }
    void unlock() { mutex_.unlock(); }

private:
    alignas(64) std::mutex mutex_;
};

class SpinLock {
public:
    void lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    void unlock() {
        // Release publishes queue updates before another thread acquires the lock.
        flag_.clear(std::memory_order_release);
    }

private:
    alignas(64) std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

template <typename Lock, std::size_t Capacity>
class SpscQueue {
    static_assert(Capacity > 0, "Queue capacity must be positive");

public:
    bool push(const Item& item) {
        std::lock_guard<Lock> guard(lock_);
        if (size_ == Capacity) {
            return false;
        }

        items_[tail_ % Capacity] = item;
        ++tail_;
        ++size_;
        return true;
    }

    bool pop(Item& item) {
        std::lock_guard<Lock> guard(lock_);
        if (size_ == 0) {
            return false;
        }

        item = items_[head_ % Capacity];
        ++head_;
        --size_;
        return true;
    }

private:
    Lock lock_;
    alignas(64) std::size_t head_ = 0;
    alignas(64) std::size_t tail_ = 0;
    alignas(64) std::size_t size_ = 0;
    ItemPool<Capacity> items_;
};
