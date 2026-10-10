#pragma once
#include <array>
#include <atomic>
#include <cstddef>

template<class T, std::size_t Capacity>
class OptimizedSPSC {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
public:
    bool push(const T& value) noexcept {
        const auto tail = local_tail_;
        if (tail - cached_head_ == Capacity) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (tail - cached_head_ == Capacity) return false;
        }

        buffer_[tail & (Capacity - 1)] = value;
        local_tail_ = tail + 1;
        tail_.store(local_tail_, std::memory_order_release);
        return true;
    }

    bool pop(T& value) noexcept {
        const auto head = local_head_;
        if (head == cached_tail_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (head == cached_tail_) return false;
        }

        value = buffer_[head & (Capacity - 1)];
        local_head_ = head + 1;
        head_.store(local_head_, std::memory_order_release);
        return true;
    }

private:
    std::array<T, Capacity> buffer_{};
    alignas(64) std::atomic<std::size_t> tail_{0};
    alignas(64) std::atomic<std::size_t> head_{0};

    // The producer thread owns local_tail_ and cached_head_.
    // The consumer thread owns local_head_ and cached_tail_.
    std::size_t local_tail_{0};
    std::size_t cached_head_{0};
    std::size_t local_head_{0};
    std::size_t cached_tail_{0};
};
