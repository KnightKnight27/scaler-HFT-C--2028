#pragma once

#include <atomic>

class Spinlock {
    public:
        void unlock() {
            Flag.store(false, std::memory_order_release);
        }
        void lock() {
            while (Flag.exchange(true, std::memory_order_acquire)){}
        }
    private:
        std::atomic<bool> Flag{false};
};