#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <stdexcept>
template <typename T>

class AtomicSPSCQueue
{
private:
    struct Element
    {
        alignas(T) std::byte storage[sizeof(T)];
    };
    std::size_t const capacity;
    std::atomic<std::size_t> mPushIdx = 0;
    std::atomic<std::size_t> mPopIdx = 0;
    Element *buffer;

public:
    AtomicSPSCQueue(const AtomicSPSCQueue &) = delete;
    AtomicSPSCQueue &operator=(const AtomicSPSCQueue &) = delete;

    AtomicSPSCQueue(std::size_t capacity) : capacity{capacity}
    {
        if (capacity == 0)
            throw std::invalid_argument("capacity must be >0");
        buffer = static_cast<Element *>(std::malloc(capacity * sizeof(Element)));
        if (!buffer)
            throw std::bad_alloc();
    }

    bool push(const T &item)
    {
        std::size_t const currTail = mPushIdx.load(std::memory_order_relaxed);
        std::size_t const nextTail = (currTail + 1) % capacity;

        if (nextTail == mPopIdx.load(std::memory_order_acquire))
            return false;

        std::construct_at(reinterpret_cast<T *>(&buffer[currTail]), item);
        mPushIdx.store(nextTail, std::memory_order_release);
        return true;
    }

    template <typename F>
    bool pop(F &&func)
    {
        std::size_t const currHead = mPopIdx.load(std::memory_order_relaxed);
        if (currHead == mPushIdx.load(std::memory_order_acquire))
            return false;

        T &elem = *reinterpret_cast<T *>(&buffer[currHead]);
        func(elem);
        std::destroy_at(&elem);
        mPopIdx.store((currHead + 1) % capacity, std::memory_order_release);
        return true;
    }

    ~AtomicSPSCQueue()
    {
        auto noop = [](T &) {};
        while (pop(noop))
            ;
        std::free(buffer);
    }
};