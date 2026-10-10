#include <atomic>

class SpinLock {
    public:
        void lock() {
            while (mFlag.exchange(true, std::memory_order_acquire)){}
        }
        void unlock() {
            mFlag.store(false, std::memory_order_release);
        }
    private:
        std::atomic<bool> mFlag{false};
};