#ifndef SPSCQUEUE_H
#define SPSCQUEUE_H

// Single-producer / single-consumer queue protected by a lock.
//
// The queue is a fixed-size ring buffer allocated once up front (a pool of
// Capacity slots), so push and pop never allocate. Every operation takes the
// lock, so the producer and consumer threads never touch the indices at the
// same time.
//
// The lock type is a template parameter, so the exact same queue code is
// measured with a spinlock (a while loop) and with std::mutex:
//
//   SPSCQueue<T, N, SpinLock>    or  SpinQueue<T, N>
//   SPSCQueue<T, N, std::mutex>  or  MutexQueue<T, N>

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

// Tells the CPU we are busy-waiting (saves power and frees resources for the
// other hyper-thread on the same core).
inline void cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#elif defined(__aarch64__)
    asm volatile("yield");
#endif
}

// Spinlock: a thread that wants the lock keeps looping (spinning) until it is
// free, instead of being put to sleep by the OS like with std::mutex.
// This is a "test-and-test-and-set" lock: while waiting it only reads the flag
// (cheap, served from its own cache) and tries the expensive exchange again
// only once the lock looks free.
class SpinLock
{
public:
    void lock() {
        for (;;) {
            if (!m_locked.exchange(true, std::memory_order_acquire)) {
                return;
            }
            while (m_locked.load(std::memory_order_relaxed)) {
                cpu_relax();
            }
        }
    }

    bool try_lock() {
        return !m_locked.load(std::memory_order_relaxed) && !m_locked.exchange(true, std::memory_order_acquire);
    }

    void unlock() { m_locked.store(false, std::memory_order_release); }

private:
    std::atomic<bool> m_locked{false};
};

template <typename T, size_t Capacity, typename Lock = SpinLock>
class SPSCQueue
{
public:
    SPSCQueue() : m_buffer(std::make_unique<T[]>(Capacity)) {}

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    bool push(const T& val) { return push_impl(val); }
    bool push(T&& val) { return push_impl(std::move(val)); }

    bool pop(T& out) {
        std::lock_guard<Lock> guard(m_lock);
        if (m_head == m_tail) {
            return false;
        }
        out = std::move(m_buffer[m_head % Capacity]);
        ++m_head;
        return true;
    }

    [[nodiscard]] size_t size() const {
        std::lock_guard<Lock> guard(m_lock);
        return m_tail - m_head;
    }

    [[nodiscard]] bool empty() const { return size() == 0; }
    [[nodiscard]] bool full() const { return size() == Capacity; }
    [[nodiscard]] static constexpr size_t capacity() { return Capacity; }

private:
    template <typename U>
    bool push_impl(U&& val) {
        std::lock_guard<Lock> guard(m_lock);
        if (m_tail - m_head == Capacity) {
            return false;
        }
        m_buffer[m_tail % Capacity] = std::forward<U>(val);
        ++m_tail;
        return true;
    }

    mutable Lock m_lock;
    size_t m_head{0};  // items ever popped
    size_t m_tail{0};  // items ever pushed
    std::unique_ptr<T[]> m_buffer;
};

template <typename T, size_t Capacity>
using MutexQueue = SPSCQueue<T, Capacity, std::mutex>;

template <typename T, size_t Capacity>
using SpinQueue = SPSCQueue<T, Capacity, SpinLock>;

#endif // SPSCQUEUE_H
