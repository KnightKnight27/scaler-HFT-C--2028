#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>


struct alignas(64) Packet {
    std::uint64_t seq;
    std::uint64_t body[7];
};
static_assert(sizeof(Packet) == 64, "Packet must be 64 bytes");


class SpinLock {
public:
    void lock() {
        for (;;) {
            if (!busy_.exchange(true, std::memory_order_acquire)) return;
            while (busy_.load(std::memory_order_relaxed)) {
            }
        }
    }
    void unlock() { busy_.store(false, std::memory_order_release); }

private:
    std::atomic<bool> busy_{false};
};

template <typename T, std::size_t N, typename Lock>
class LockedRing {
    static_assert(N && (N & (N - 1)) == 0, "N must be a power of two");

public:
    bool try_push(const T& item) {
        std::lock_guard<Lock> guard(lock_);
        if (write_ - read_ == N) return false;  // full
        slots_[write_ & (N - 1)] = item;
        ++write_;
        return true;
    }

    bool try_pop(T& item) {
        std::lock_guard<Lock> guard(lock_);
        if (write_ == read_) return false;  // empty
        item = slots_[read_ & (N - 1)];
        ++read_;
        return true;
    }

private:
    T slots_[N];
    std::size_t write_ = 0;  // total items ever pushed
    std::size_t read_ = 0;   // total items ever popped
    Lock lock_;
};
