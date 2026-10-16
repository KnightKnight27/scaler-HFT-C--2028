#include <vector>
#include <mutex>
#include <cstddef>
#include <utility>
#include <stdexcept>
template <typename T>

class MutexSPSCQueue
{
private:
    std::vector<T> buffer;
    std::mutex mutex;
    std::size_t const capacity;
    std::size_t mPushIdx = 0;
    std::size_t mPopIdx = 0;

public:
    MutexSPSCQueue(std::size_t capacity) : capacity{capacity}
    {
        if (capacity == 0)
            throw std::invalid_argument("Capacity must be > 0");
        buffer.resize(capacity);
    }

    MutexSPSCQueue(const MutexSPSCQueue &) = delete;
    MutexSPSCQueue &operator=(const MutexSPSCQueue &) = delete;

    bool push(const T &item)
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::size_t next_tail = (mPushIdx + 1) % capacity;
        if (next_tail == mPopIdx)
            return false;
        buffer[mPushIdx] = item;
        mPushIdx = next_tail;
        return true;
    }

    template <typename F>
    bool pop(F &&func)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (mPushIdx == mPopIdx)
        {
            return false;
        }

        T elem = std::move(buffer[mPopIdx]);
        func(elem);
        mPopIdx = (mPopIdx + 1) % capacity;
        return true;
    }
};