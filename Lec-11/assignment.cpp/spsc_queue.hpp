#pragma once

#include <atomic>
#include <mutex>

// simple spinlock using atomic_flag, just keeps looping till it gets the lock
struct SpinLock {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
            // spin
        }
    }
    void unlock() {
        flag.clear(std::memory_order_release);
    }
};

// ring buffer queue. the array is allocated once at the start
// so we never call new while pushing/popping (this is the memory pool part)
// LockType can be std::mutex or SpinLock
template <typename T, typename LockType>
class SPSCQueue {
    T* buf;
    int cap;
    int head; // consumer reads from here
    int tail; // producer writes here
    int count;
    LockType lk;

public:
    SPSCQueue(int size) {
        cap = size;
        buf = new T[cap];
        head = 0;
        tail = 0;
        count = 0;
    }

    ~SPSCQueue() {
        delete[] buf;
    }

    // dont want copies of the queue
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    bool push(const T& o) {
        lk.lock();
        if (count == cap) {
            lk.unlock();
            return false; // full
        }
        buf[tail] = o;
        tail++;
        if (tail == cap) tail = 0;
        count++;
        lk.unlock();
        return true;
    }

    bool pop(T& o) {
        lk.lock();
        if (count == 0) {
            lk.unlock();
            return false; // empty
        }
        o = buf[head];
        head++;
        if (head == cap) head = 0;
        count--;
        lk.unlock();
        return true;
    }

    int size() {
        lk.lock();
        int c = count;
        lk.unlock();
        return c;
    }

    bool empty() { return size() == 0; }
    bool full() { return size() == cap; }
    int capacity() { return cap; }
};
