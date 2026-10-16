#pragma once

#include <array>
#include <cstddef>
#include <mutex>
#include <utility>

template <typename T, std::size_t N>
class SPSCQueue {
    static_assert(N > 0, "Queue capacity must be greater than zero");

public:
    bool push(const T& item) {
        std::lock_guard<std::mutex> guard(lock_);
        if (count_ == N) {
            return false;
        }

        storage_[write_index_] = item;
        write_index_ = next_index(write_index_);
        ++count_;
        return true;
    }

    bool push(T&& item) {
        std::lock_guard<std::mutex> guard(lock_);
        if (count_ == N) {
            return false;
        }

        storage_[write_index_] = std::move(item);
        write_index_ = next_index(write_index_);
        ++count_;
        return true;
    }

    bool pop(T& item) {
        std::lock_guard<std::mutex> guard(lock_);
        if (count_ == 0) {
            return false;
        }

        item = std::move(storage_[read_index_]);
        read_index_ = next_index(read_index_);
        --count_;
        return true;
    }

    bool is_empty() const {
        std::lock_guard<std::mutex> guard(lock_);
        return count_ == 0;
    }

    bool is_full() const {
        std::lock_guard<std::mutex> guard(lock_);
        return count_ == N;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> guard(lock_);
        return count_;
    }

    constexpr std::size_t capacity() const noexcept {
        return N;
    }

private:
    static constexpr std::size_t next_index(std::size_t index) noexcept {
        return (index + 1 == N) ? 0 : index + 1;
    }

    std::array<T, N> storage_{};
    std::size_t read_index_{0};
    std::size_t write_index_{0};
    std::size_t count_{0};
    mutable std::mutex lock_;
};
