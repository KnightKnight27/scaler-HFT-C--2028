#pragma once
#include <array>
#include <cstddef>
#include <mutex>

template<class T, std::size_t Capacity>
class LockSPSC {
    static_assert(Capacity > 0);
public:
    bool push(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == Capacity) return false;
        buffer_[tail_] = value;
        tail_ = (tail_ + 1) % Capacity;
        ++count_;
        return true;
    }

    bool pop(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == 0) return false;
        value = buffer_[head_];
        head_ = (head_ + 1) % Capacity;
        --count_;
        return true;
    }

private:
    std::array<T, Capacity> buffer_{};
    std::size_t head_{0}, tail_{0}, count_{0};
    std::mutex mutex_;
};
