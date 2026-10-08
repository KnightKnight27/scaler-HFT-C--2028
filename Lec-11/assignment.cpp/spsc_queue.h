#pragma once

#include <array>
#include <cstddef>
#include <mutex>

using namespace std;

template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert(Capacity > 0, "Capacity must be positive");

public:
    bool push(const T& value) {
        lock_guard lock(mutex_);
        if (size_ == Capacity){
            return false;
        }

        buffer_[tail_] = value;
        tail_ = (tail_ + 1) % Capacity;
        ++size_;
        return true;
    }

    bool pop(T& value) {
        lock_guard lock(mutex_);
        if (size_ == 0){

            return false;
        }

        value = buffer_[head_];
        head_ = (head_ + 1) % Capacity;
        --size_;
        return true;
    }

    bool empty() const {
        lock_guard lock(mutex_);
        return size_ == 0;
    }

private:
    array<T, Capacity> buffer_{};
    mutable mutex mutex_;
    size_t head_{0};
    size_t tail_{0};
    size_t size_{0};
};