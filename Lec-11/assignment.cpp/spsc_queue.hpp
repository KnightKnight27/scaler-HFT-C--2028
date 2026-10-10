#ifndef SPSC_QUEUE_HPP
#define SPSC_QUEUE_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

struct alignas(64) Item {
    std::byte payload[64];
};

static_assert(sizeof(Item) == 64, "Item must occupy exactly one 64-byte cache line");

class MutexLock {
public:
    void lock();
    void unlock();

private:
    std::mutex mutex_;
};

class SpinLock {
public:
    SpinLock();
    void lock();
    void unlock();

private:
    std::atomic_flag flag_;
};

template <typename Lock>
class SPSCQueue {
public:
    static constexpr std::uint64_t capacity = 1024;

    bool push(const Item& item);
    bool pop(Item& item);

private:
    Lock lock_;
    std::array<Item, capacity> buffer_{};
    std::uint64_t head_ = 0;
    std::uint64_t tail_ = 0;
};

extern template class SPSCQueue<MutexLock>;
extern template class SPSCQueue<SpinLock>;

#endif
