#pragma once

#include <atomic>
#include <array>

// Quick spinlock - std::mutex is too slow for high throughput SPSC
class SpinLock {
    std::atomic_flag locked = ATOMIC_FLAG_INIT;
public:
    void lock() {
        // spin while locked
        while (locked.test_and_set(std::memory_order_acquire)) { 
            // _mm_pause(); // uncomment if on x86 for slightly better thermals/perf
        }
    }
    void unlock() {
        locked.clear(std::memory_order_release);
    }
};

template <typename T, std::size_t Cap>
class SPSCQueue {
    std::array<T, Cap> buffer;
    int head = 0;
    int tail = 0;
    SpinLock mtx; // lock per queue

public:
    SPSCQueue() = default;

    // return false if full
    bool push(const T& val) {
        mtx.lock();
        int next_head = (head + 1) % Cap;

        if (next_head == tail) {
            mtx.unlock();
            return false;
        }

        buffer[head] = val;
        head = next_head;
        mtx.unlock();
        return true;
    }

    // Pass by ref to avoid the weird front() + pop() split
    bool pop(T& out_val) {
        mtx.lock();
        if (head == tail) {
            mtx.unlock();
            return false; // empty
        }

        out_val = buffer[tail];
        tail = (tail + 1) % Cap;
        mtx.unlock();
        return true;
    }
};
