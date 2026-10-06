#pragma once

#include <array>
#include <cstddef>
#include <mutex>

template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert(Capacity > 0, "Capacity must be positive");

public:
    bool push(const T& value) {
        std::lock_guard lock(mutex_);
        if (size_ == Capacity)
            return false;

        buffer_[tail_] = value;
        tail_ = (tail_ + 1) % Capacity;
        ++size_;
        return true;
    }

    bool pop(T& value) {
        std::lock_guard lock(mutex_);
        if (size_ == 0)
            return false;

        value = buffer_[head_];
        head_ = (head_ + 1) % Capacity;
        --size_;
        return true;
    }

    bool empty() const {
        std::lock_guard lock(mutex_);
        return size_ == 0;
    }

private:
    std::array<T, Capacity> buffer_{};
    mutable std::mutex mutex_;
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t size_{0};
};
