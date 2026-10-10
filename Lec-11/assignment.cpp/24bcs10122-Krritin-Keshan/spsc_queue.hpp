// spsc_queue.hpp — SPSC ring buffer protected by a lock (std::mutex or SpinLock)
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

// 64 byte object (one cache line)
struct Message {
    std::uint64_t data[8];
};
static_assert(sizeof(Message) == 64, "Message must be 64 bytes");

// spinlock: just a while loop on an atomic flag
class SpinLock {
public:
    void lock() {
        while (mFlag.test_and_set(std::memory_order_acquire)) {}
    }
    void unlock() { mFlag.clear(std::memory_order_release); }

private:
    std::atomic_flag mFlag = ATOMIC_FLAG_INIT;
};

// MEMORY POOL: one fixed array allocated once, no new/delete per push/pop
// SIZE POWER OF 2 -> idx & (SIZE-1) instead of idx % SIZE
template <typename T, std::size_t Size, typename Lock>
class SPSCQueue {
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");

public:
    // producer thread only
    bool push(const T& val) {
        std::lock_guard<Lock> guard(mLock);
        if (mPushIdx - mPopIdx == Size) [[unlikely]]   // full
            return false;
        mData[mPushIdx & (Size - 1)] = val;
        mPushIdx++;
        return true;
    }

    // consumer thread only
    bool pop(T& val) {
        std::lock_guard<Lock> guard(mLock);
        if (mPushIdx == mPopIdx) [[unlikely]]          // empty
            return false;
        val = mData[mPopIdx & (Size - 1)];
        mPopIdx++;
        return true;
    }

private:
    Lock mLock;
    std::size_t mPushIdx{0};   // push / pop idx only increase, guarded by mLock
    std::size_t mPopIdx{0};
    std::array<T, Size> mData;
};
