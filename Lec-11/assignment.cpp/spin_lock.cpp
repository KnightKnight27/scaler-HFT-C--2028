#include <atomic>

class SpinLock
{
public:
    void lock()
    {
        while (flag_.exchange(true, std::memory_order_acquire))
        {
            // busy wait until the lock is released
        }
    }

    void unlock()
    {
        flag_.store(false, std::memory_order_release);
    }

private:
    std::atomic<bool> flag_ { false };
};
