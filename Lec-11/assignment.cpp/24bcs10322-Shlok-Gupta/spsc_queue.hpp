#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

// One cache-line-sized object: 8 * 8 bytes = 64 bytes.
struct Object {
    std::uint64_t data[8]{};
};

static_assert(sizeof(Object) == 64, "Object must be exactly 64 bytes");

// A simple spinlock. A thread repeatedly checks the flag while it is locked.
class SpinLock {
public:
    SpinLock() noexcept = default;
    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

    void lock() noexcept {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // Busy-wait until the lock becomes available.
        }
    }

    void unlock() noexcept {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

// Fixed-capacity ring buffer protected by the supplied lock type.
// push() returns false when full; pop() returns false when empty.
template <std::size_t Capacity, typename Lock>
class SpscQueue {
    static_assert(Capacity > 0, "Queue capacity must be greater than zero");

public:
    bool push(const Object& object) {
        std::lock_guard<Lock> guard(lock_);

        if (count_ == Capacity) {
            return false;
        }

        buffer_[tail_] = object;
        tail_ = (tail_ + 1) % Capacity;
        ++count_;
        return true;
    }

    bool pop(Object& output) {
        std::lock_guard<Lock> guard(lock_);

        if (count_ == 0) {
            return false;
        }

        output = buffer_[head_];
        head_ = (head_ + 1) % Capacity;
        --count_;
        return true;
    }

private:
    std::array<Object, Capacity> buffer_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t count_ = 0;
    Lock lock_{};
};
