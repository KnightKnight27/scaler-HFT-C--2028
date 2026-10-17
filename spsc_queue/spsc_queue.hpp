#pragma once

#include <atomic>
#include <array>
#include <mutex>
#include <cstddef>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define CPU_RELAX() _mm_pause()
#elif defined(__aarch64__)
#define CPU_RELAX() asm volatile("yield" ::: "memory")
#else
#define CPU_RELAX() ((void)0)
#endif

// Hardware cache line alignment constant (64 bytes on x86 and standard ARM, 128 on Apple Silicon)
constexpr std::size_t kCacheLineSize = 64;

//=============================================================================
// 1. Naive Test-and-Set Spinlock
//=============================================================================
class SpinLock {
private:
    std::atomic<bool> flag_{false};

public:
    void lock() noexcept {
        while (flag_.exchange(true, std::memory_order_acquire)) {
            // Tight loop: atomic RMW causes cache line bouncing on every iteration
        }
    }

    void unlock() noexcept {
        flag_.store(false, std::memory_order_release);
    }
};

//=============================================================================
// 2. Optimized TTAS (Test-and-Test-and-Set) SpinLock
//=============================================================================
class TTASSpinLock {
private:
    std::atomic<bool> flag_{false};

public:
    void lock() noexcept {
        while (true) {
            // Spin on read-only load in 'Shared' cache state without bus invalidation
            while (flag_.load(std::memory_order_relaxed)) {
                CPU_RELAX();
            }
            // Only issue an atomic write (RMW) when the flag is observed free
            if (!flag_.exchange(true, std::memory_order_acquire)) {
                return;
            }
        }
    }

    void unlock() noexcept {
        flag_.store(false, std::memory_order_release);
    }
};

//=============================================================================
// 3. Locked SPSC Queue (Templated on Lock type)
//=============================================================================
template <typename T, typename Lock = TTASSpinLock, std::size_t Capacity = 65536>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr std::size_t kMask = Capacity - 1;

private:
    std::size_t head_{0};
    std::size_t tail_{0};
    std::array<T, Capacity> buffer_{};
    Lock lock_{};

public:
    SPSCQueue() = default;

    bool push(const T& item) {
        lock_.lock();
        if (tail_ - head_ == Capacity) {
            lock_.unlock();
            return false; // Queue is full
        }
        buffer_[tail_ & kMask] = item;
        ++tail_;
        lock_.unlock();
        return true;
    }

    bool pop(T& item) {
        lock_.lock();
        if (head_ == tail_) {
            lock_.unlock();
            return false; // Queue is empty
        }
        item = buffer_[head_ & kMask];
        ++head_;
        lock_.unlock();
        return true;
    }

    std::size_t size() {
        lock_.lock();
        std::size_t s = tail_ - head_;
        lock_.unlock();
        return s;
    }

    bool empty() {
        lock_.lock();
        bool e = (head_ == tail_);
        lock_.unlock();
        return e;
    }
};

// Aliases for convenience
template <typename T, std::size_t Capacity = 65536>
using SPSCQueueSpinLock = SPSCQueue<T, SpinLock, Capacity>;

template <typename T, std::size_t Capacity = 65536>
using SPSCQueueTTASSpinLock = SPSCQueue<T, TTASSpinLock, Capacity>;

template <typename T, std::size_t Capacity = 65536>
using SPSCQueueMutex = SPSCQueue<T, std::mutex, Capacity>;

//=============================================================================
// 4. Naive Lock-Free SPSC Queue WITH False Sharing (head & tail in same cache line)
//=============================================================================
template <typename T, std::size_t Capacity = 65536>
class LockFreeSPSCQueueNaive {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr std::size_t kMask = Capacity - 1;

private:
    // Placed side-by-side without alignment: 8 bytes apart, sharing a 64-byte cache line!
    std::atomic<std::size_t> tail_{0};
    std::atomic<std::size_t> head_{0};
    std::array<T, Capacity> buffer_{};

public:
    LockFreeSPSCQueueNaive() = default;

    bool push(const T& item) {
        const std::size_t current_tail = tail_.load(std::memory_order_relaxed);
        if (current_tail - head_.load(std::memory_order_acquire) == Capacity) {
            return false;
        }
        buffer_[current_tail & kMask] = item;
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        const std::size_t current_head = head_.load(std::memory_order_relaxed);
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        item = buffer_[current_head & kMask];
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    std::ptrdiff_t index_gap_bytes() const {
        return static_cast<std::ptrdiff_t>(
            reinterpret_cast<std::uintptr_t>(&head_) -
            reinterpret_cast<std::uintptr_t>(&tail_));
    }

    bool empty() const {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }
};

//=============================================================================
// 5. Lock-Free SPSC Queue with alignas(64) to eliminate False Sharing
//=============================================================================
template <typename T, std::size_t Capacity = 65536>
class LockFreeSPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr std::size_t kMask = Capacity - 1;

private:
    // alignas(64) ensures tail_ and head_ occupy separate cache lines
    // preventing false sharing between producer and consumer cores.
    alignas(kCacheLineSize) std::atomic<std::size_t> tail_{0};
    alignas(kCacheLineSize) std::atomic<std::size_t> head_{0};
    alignas(kCacheLineSize) std::array<T, Capacity> buffer_{};

public:
    LockFreeSPSCQueue() = default;

    // Producer thread only
    bool push(const T& item) {
        const std::size_t current_tail = tail_.load(std::memory_order_relaxed);
        if (current_tail - head_.load(std::memory_order_acquire) == Capacity) {
            return false;
        }
        buffer_[current_tail & kMask] = item;
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    // Consumer thread only
    bool pop(T& item) {
        const std::size_t current_head = head_.load(std::memory_order_relaxed);
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        item = buffer_[current_head & kMask];
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    std::ptrdiff_t index_gap_bytes() const {
        return static_cast<std::ptrdiff_t>(
            reinterpret_cast<std::uintptr_t>(&head_) -
            reinterpret_cast<std::uintptr_t>(&tail_));
    }

    std::size_t size() const {
        std::size_t t = tail_.load(std::memory_order_relaxed);
        std::size_t h = head_.load(std::memory_order_relaxed);
        return (t >= h) ? (t - h) : (Capacity - (h - t));
    }

    bool empty() const {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }
};

//=============================================================================
// 6. High-Performance Lock-Free SPSC Queue with Shadow Cached Indices
//=============================================================================
template <typename T, std::size_t Capacity = 65536>
class LockFreeSPSCQueueCached {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr std::size_t kMask = Capacity - 1;

private:
    // Producer cache line: tail_ and cached head_
    alignas(kCacheLineSize) std::atomic<std::size_t> tail_{0};
    std::size_t cached_head_{0};

    // Consumer cache line: head_ and cached tail_
    alignas(kCacheLineSize) std::atomic<std::size_t> head_{0};
    std::size_t cached_tail_{0};

    // Buffer cache aligned
    alignas(kCacheLineSize) std::array<T, Capacity> buffer_{};

public:
    LockFreeSPSCQueueCached() = default;

    // Producer thread only
    bool push(const T& item) {
        const std::size_t current_tail = tail_.load(std::memory_order_relaxed);
        // Check local cached_head_ first to avoid cross-core atomic load
        if (current_tail - cached_head_ == Capacity) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (current_tail - cached_head_ == Capacity) {
                return false; // Truly full
            }
        }
        buffer_[current_tail & kMask] = item;
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    // Consumer thread only
    bool pop(T& item) {
        const std::size_t current_head = head_.load(std::memory_order_relaxed);
        // Check local cached_tail_ first to avoid cross-core atomic load
        if (current_head == cached_tail_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (current_head == cached_tail_) {
                return false; // Truly empty
            }
        }
        item = buffer_[current_head & kMask];
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    std::size_t size() const {
        std::size_t t = tail_.load(std::memory_order_relaxed);
        std::size_t h = head_.load(std::memory_order_relaxed);
        return (t >= h) ? (t - h) : (Capacity - (h - t));
    }

    bool empty() const {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }
};
