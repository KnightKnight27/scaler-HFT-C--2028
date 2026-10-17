#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace assignment {

// Eight words are transferred by value; no pointer to external storage is queued.
struct Object {
    std::array<std::uint64_t, 8> words{};
};
static_assert(sizeof(Object) == 64, "The assignment requires 64-byte objects");

inline Object make_object(std::uint64_t sequence) {
    Object value;
    for (std::size_t i = 0; i < value.words.size(); ++i) {
        value.words[i] = sequence ^ (0x9e3779b97f4a7c15ULL * (i + 1));
    }
    return value;
}

inline bool valid_object(const Object &value, std::uint64_t sequence) {
    return value.words == make_object(sequence).words;
}

class SpinLock {
  public:
    void lock() noexcept {
        unsigned attempts = 0;
        while (flag_.test_and_set(std::memory_order_acquire)) {
            if (++attempts == 64) {
                attempts = 0;
                std::this_thread::yield();
            }
        }
    }
    void unlock() noexcept { flag_.clear(std::memory_order_release); }

  private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

// A bounded, lock-based FIFO intended for one producer and one consumer.
// The preallocated slot pool is reused; push/pop perform no heap allocation.
// The same lock guards storage, read/write positions, and occupied count.
template <class Lock = std::mutex> class SPSCQueue {
  public:
    explicit SPSCQueue(std::size_t capacity) : slots_(checked_capacity(capacity)) {}
    SPSCQueue(const SPSCQueue &) = delete;
    SPSCQueue &operator=(const SPSCQueue &) = delete;

    bool try_push(const Object &value) {
        std::lock_guard<Lock> guard(lock_);
        if (size_ == slots_.size()) {
            return false;
        }
        slots_[tail_] = value;
        tail_ = next(tail_);
        ++size_;
        return true;
    }

    bool try_pop(Object &value) {
        std::lock_guard<Lock> guard(lock_);
        if (size_ == 0) {
            return false;
        }
        value = slots_[head_];
        head_ = next(head_);
        --size_;
        return true;
    }

  private:
    static std::size_t checked_capacity(std::size_t capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("queue capacity must be positive");
        }
        return capacity;
    }
    std::size_t next(std::size_t position) const noexcept {
        return position + 1 == slots_.size() ? 0 : position + 1;
    }

    Lock lock_;
    std::vector<Object> slots_;
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t size_ = 0;
};

} // namespace assignment
