#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>

// A BasicLockable lock, so the same queue can use lock_guard with either lock.
class SpinLock {
public:
    void lock() noexcept {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    void unlock() noexcept {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

// Intended for one producer and one consumer. Each operation holds the lock
// while checking the state, copying the object, and updating the ring indices.
template <typename T, typename Lock = std::mutex>
class SPSCQueue {
    static_assert(std::is_trivially_copyable<T>::value,
                  "This queue stores trivially copyable objects");
    static_assert(std::is_nothrow_copy_assignable<T>::value,
                  "Copying an object must not throw");

public:
    explicit SPSCQueue(std::size_t capacity)
        : capacity_(checked_capacity(capacity)),
          slots_(std::make_unique<T[]>(capacity_)) {}

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;

    // false means full; the caller can retry after the consumer frees a slot.
    bool try_push(const T& value) {
        std::lock_guard<Lock> guard(lock_);
        if (size_ == capacity_) {
            return false;
        }
        slots_[tail_] = value;
        tail_ = next(tail_);
        ++size_;
        return true;
    }

    // false means empty; out is unchanged on failure.
    bool try_pop(T& out) {
        std::lock_guard<Lock> guard(lock_);
        if (size_ == 0) {
            return false;
        }
        out = slots_[head_];
        head_ = next(head_);
        --size_;
        return true;
    }

    std::size_t capacity() const noexcept { return capacity_; }

private:
    static std::size_t checked_capacity(std::size_t capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("Queue capacity must be positive");
        }
        return capacity;
    }

    std::size_t next(std::size_t index) const noexcept {
        return index + 1 == capacity_ ? 0 : index + 1;
    }

    const std::size_t capacity_;
    // The buffer is the fixed pool: allocate once and reuse its slots.
    std::unique_ptr<T[]> slots_;
    Lock lock_;
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t size_ = 0;
};
