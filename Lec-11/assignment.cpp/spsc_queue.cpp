#include "spsc_queue.hpp"

void MutexLock::lock()
{
    mutex_.lock();
}

void MutexLock::unlock()
{
    mutex_.unlock();
}

SpinLock::SpinLock()
    : flag_(ATOMIC_FLAG_INIT)
{
}

void SpinLock::lock()
{
    while (flag_.test_and_set(std::memory_order_acquire)) {
    }
}

void SpinLock::unlock()
{
    flag_.clear(std::memory_order_release);
}

template <typename Lock>
bool SPSCQueue<Lock>::push(const Item& item)
{
    std::lock_guard<Lock> guard(lock_);
    if (head_ - tail_ == capacity) {
        return false;
    }

    buffer_[head_ & (capacity - 1)] = item;
    ++head_;
    return true;
}

template <typename Lock>
bool SPSCQueue<Lock>::pop(Item& item)
{
    std::lock_guard<Lock> guard(lock_);
    if (head_ == tail_) {
        return false;
    }

    item = buffer_[tail_ & (capacity - 1)];
    ++tail_;
    return true;
}

template class SPSCQueue<MutexLock>;
template class SPSCQueue<SpinLock>;

static_assert((SPSCQueue<MutexLock>::capacity &
               (SPSCQueue<MutexLock>::capacity - 1)) == 0,
              "Queue capacity must be a power of two");
