#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <thread>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#include <immintrin.h>
#define CPU_RELAX() _mm_pause()
#elif defined(__aarch64__)
#define CPU_RELAX() asm volatile("yield")
#else
#define CPU_RELAX() std::this_thread::yield()
#endif

// plain spinlock: keep doing test_and_set in a while loop till we get it
struct SpinLock {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
            // spin
        }
    }
    void unlock() { flag.clear(std::memory_order_release); }
};

// test-and-test-and-set spinlock: while someone holds it we only *read*
// (cheap, cache line stays shared) and only try the write when it looks free
struct TTASSpinLock {
    std::atomic<bool> locked{false};

    void lock() {
        for (;;) {
            if (!locked.exchange(true, std::memory_order_acquire)) return;
            while (locked.load(std::memory_order_relaxed)) CPU_RELAX();
        }
    }
    void unlock() { locked.store(false, std::memory_order_release); }
};

// bounded ring buffer, one producer one consumer, guarded by a lock.
// all slots are allocated once in the constructor (memory pool), push/pop
// just copy into / out of a preallocated slot - no new/delete on the hot path.
// capacity is rounded up to a power of 2 so wrap around is a bit mask.
template <typename T, typename Lock>
class SPSCQueue {
public:
    explicit SPSCQueue(std::size_t want) {
        cap_ = 1;
        while (cap_ < want) cap_ <<= 1;
        mask_ = cap_ - 1;
        pool_ = new T[cap_];
    }
    ~SPSCQueue() { delete[] pool_; }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    bool push(const T& v) {
        std::lock_guard<Lock> g(lock_);
        if (tail_ - head_ == cap_) return false; // full
        pool_[tail_ & mask_] = v;
        ++tail_;
        return true;
    }

    bool pop(T& out) {
        std::lock_guard<Lock> g(lock_);
        if (tail_ == head_) return false; // empty
        out = pool_[head_ & mask_];
        ++head_;
        return true;
    }

    // batch versions: grab the lock once and move up to n items
    std::size_t push_n(const T* v, std::size_t n) {
        std::lock_guard<Lock> g(lock_);
        std::size_t space = cap_ - (tail_ - head_);
        if (n > space) n = space;
        for (std::size_t i = 0; i < n; ++i) pool_[(tail_ + i) & mask_] = v[i];
        tail_ += n;
        return n;
    }

    std::size_t pop_n(T* out, std::size_t n) {
        std::lock_guard<Lock> g(lock_);
        std::size_t have = tail_ - head_;
        if (n > have) n = have;
        for (std::size_t i = 0; i < n; ++i) out[i] = pool_[(head_ + i) & mask_];
        head_ += n;
        return n;
    }

    std::size_t size() {
        std::lock_guard<Lock> g(lock_);
        return tail_ - head_;
    }
    bool empty() { return size() == 0; }
    bool full() { return size() == cap_; }
    std::size_t capacity() const { return cap_; }

private:
    T* pool_ = nullptr;
    std::size_t cap_ = 0;
    std::size_t mask_ = 0;
    std::size_t head_ = 0; // total popped (consumer side)
    std::size_t tail_ = 0; // total pushed (producer side)
    Lock lock_;
};
