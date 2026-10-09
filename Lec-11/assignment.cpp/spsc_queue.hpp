// SPSC Assignment - Rohan Ranjan - 10428
//
// the queue, the spinlock and the memory pool live here
// so both spsc_queue.cpp (benchmark) and test.cpp can use them

#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

// tell the cpu we are spinning (makes the busy wait a bit nicer)
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define CPU_RELAX() _mm_pause()
#elif defined(__aarch64__)
#define CPU_RELAX() asm volatile("yield")
#else
#define CPU_RELAX() ((void)0)
#endif

// simple spinlock, keeps looping in a while loop till it gets the lock
class SpinLock {
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
public:
    void lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) CPU_RELAX();
    }
    void unlock() { flag_.clear(std::memory_order_release); }
};

// memory pool: one big block for all the slots, allocated once at the start
// so push / pop never call new or delete
template <typename T>
class MemoryPool {
    T*     slots_;
    size_t n_;
public:
    explicit MemoryPool(size_t n) : n_(n) {
        size_t bytes = (n * sizeof(T) + 63) / 64 * 64; // aligned_alloc needs a multiple of 64
        slots_ = static_cast<T*>(std::aligned_alloc(64, bytes));
        if (!slots_) { std::perror("aligned_alloc"); std::exit(1); }
        std::memset(static_cast<void*>(slots_), 0, bytes); // touch every page now, not during the benchmark
    }
    ~MemoryPool() { std::free(slots_); }

    // no copies
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    T& operator[](size_t i) { return slots_[i]; }
    size_t size() const { return n_; }
};

// the main thing: SPSC queue with a lock
// Lock can be SpinLock or std::mutex
// capacity has to be a power of 2 so we can use & instead of %
template <typename T, typename Lock>
class LockedSPSCQueue {
    MemoryPool<T> buf_;
    size_t        mask_;
    size_t        head_ = 0; // consumer pops from here
    size_t        tail_ = 0; // producer pushes here
    Lock          lock_;
public:
    explicit LockedSPSCQueue(size_t cap_pow2) : buf_(cap_pow2), mask_(cap_pow2 - 1) {
        assert(cap_pow2 && (cap_pow2 & (cap_pow2 - 1)) == 0 && "capacity must be a power of 2");
    }

    bool push(const T& v) {
        std::lock_guard<Lock> g(lock_);
        if (tail_ - head_ == buf_.size()) return false; // full
        buf_[tail_ & mask_] = v;
        ++tail_;
        return true;
    }

    bool pop(T& out) {
        std::lock_guard<Lock> g(lock_);
        if (head_ == tail_) return false; // empty
        out = buf_[head_ & mask_];
        ++head_;
        return true;
    }

    size_t size() {
        std::lock_guard<Lock> g(lock_);
        return tail_ - head_;
    }
};

// lock-free version, NOT part of the assignment
// only here to compare how fast it is without any lock
template <typename T>
class LockFreeSPSCQueue {
    MemoryPool<T> buf_;
    size_t        mask_;
    // each index on its own cache line so the two threads don't fight over it
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) size_t cached_tail_ = 0;   // consumer's last seen tail
    alignas(64) std::atomic<size_t> tail_{0};
    alignas(64) size_t cached_head_ = 0;   // producer's last seen head
public:
    explicit LockFreeSPSCQueue(size_t cap_pow2) : buf_(cap_pow2), mask_(cap_pow2 - 1) {
        assert(cap_pow2 && (cap_pow2 & (cap_pow2 - 1)) == 0 && "capacity must be a power of 2");
    }

    bool push(const T& v) {
        size_t t = tail_.load(std::memory_order_relaxed);
        if (t - cached_head_ == buf_.size()) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (t - cached_head_ == buf_.size()) return false; // full
        }
        buf_[t & mask_] = v;
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& out) {
        size_t h = head_.load(std::memory_order_relaxed);
        if (h == cached_tail_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (h == cached_tail_) return false; // empty
        }
        out = buf_[h & mask_];
        head_.store(h + 1, std::memory_order_release);
        return true;
    }
};
