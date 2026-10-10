#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

// Lock policy used by the queue. It deliberately has the same lock/unlock
// interface as std::mutex, so the queue can be benchmarked with either policy.
class BusyLock {
public:
    void lock() noexcept {
        while (state_.test_and_set(std::memory_order_acquire)) {
        }
    }

    void unlock() noexcept { state_.clear(std::memory_order_release); }

private:
    std::atomic_flag state_ = ATOMIC_FLAG_INIT;
};

template <class T, class Lock = std::mutex>
class SPSCQueue {
public:
    explicit SPSCQueue(std::size_t capacity)
        : capacity_(capacity), slots_(std::make_unique<T[]>(capacity)) {
        if (capacity == 0) {
            throw std::invalid_argument("SPSCQueue capacity must be positive");
        }
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    bool try_push(const T& value) {
        std::lock_guard<Lock> guard(lock_);
        if (used_ == capacity_) {
            return false;
        }
        slots_[write_] = value;
        write_ = advance(write_);
        ++used_;
        return true;
    }

    bool try_pop(T& value) {
        std::lock_guard<Lock> guard(lock_);
        if (used_ == 0) {
            return false;
        }
        value = std::move(slots_[read_]);
        read_ = advance(read_);
        --used_;
        return true;
    }

    bool empty() const {
        std::lock_guard<Lock> guard(lock_);
        return used_ == 0;
    }

    bool full() const {
        std::lock_guard<Lock> guard(lock_);
        return used_ == capacity_;
    }

    std::size_t size() const {
        std::lock_guard<Lock> guard(lock_);
        return used_;
    }

    constexpr std::size_t capacity() const noexcept { return capacity_; }

private:
    std::size_t advance(std::size_t index) const noexcept {
        return index + 1 == capacity_ ? 0 : index + 1;
    }

    const std::size_t capacity_;
    std::unique_ptr<T[]> slots_;
    std::size_t read_{0};
    std::size_t write_{0};
    std::size_t used_{0};
    mutable Lock lock_;
};
