#ifndef SPSCQUEUE_H
#define SPSCQUEUE_H

#include <array>
#include <cstddef>
#include <mutex>

template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert(Capacity > 0, "Capacity must be positive");

public:
    SPSCQueue() = default;

    bool push(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (full_nolock()) {
            return false;
        }

        data_[tail_] = value;
        tail_ = next(tail_);
        ++count_;
        return true;
    }

    bool pop(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (empty_nolock()) {
            return false;
        }

        value = data_[head_];
        head_ = next(head_);
        --count_;
        return true;
    }

    bool front(T& value) const {
        std::lock_guard<std::mutex> lock(mutex_);

        if (empty_nolock()) {
            return false;
        }

        value = data_[head_];
        return true;
    }

    bool back(T& value) const {
        std::lock_guard<std::mutex> lock(mutex_);

        if (empty_nolock()) {
            return false;
        }

        const std::size_t last = (tail_ + Capacity - 1) % Capacity;
        value = data_[last];
        return true;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return empty_nolock();
    }

    bool full() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return full_nolock();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = 0;
        tail_ = 0;
        count_ = 0;
    }

private:
    static constexpr std::size_t next(std::size_t index) {
        return (index + 1) % Capacity;
    }

    bool empty_nolock() const {
        return count_ == 0;
    }

    bool full_nolock() const {
        return count_ == Capacity;
    }

    std::array<T, Capacity> data_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t count_ = 0;
    mutable std::mutex mutex_;
};

#endif
