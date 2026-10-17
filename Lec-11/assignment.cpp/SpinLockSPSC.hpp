#pragma once
#include <array>
#include <atomic>
#include <cstddef>

class SPSCSpinLock {
public:
    void lock() noexcept {
        while (flag_.test_and_set(std::memory_order_acquire)) {
#if defined(__GNUC__) || defined(__clang__)
#if defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
#endif
#endif
        }
    }
    void unlock() noexcept { flag_.clear(std::memory_order_release); }
private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

template<class T, std::size_t Capacity>
class SpinLockSPSC {
    static_assert(Capacity > 0);
public:
    bool push(const T& value) {
        LockGuard guard(lock_);
        if (count_ == Capacity) return false;
        buffer_[tail_] = value;
        tail_ = (tail_ + 1) % Capacity;
        ++count_;
        return true;
    }

    bool pop(T& value) {
        LockGuard guard(lock_);
        if (count_ == 0) return false;
        value = buffer_[head_];
        head_ = (head_ + 1) % Capacity;
        --count_;
        return true;
    }

private:
    class LockGuard {
    public:
        explicit LockGuard(SPSCSpinLock& lock) : lock_(lock) { lock_.lock(); }
        ~LockGuard() { lock_.unlock(); }
        LockGuard(const LockGuard&) = delete;
        LockGuard& operator=(const LockGuard&) = delete;
    private:
        SPSCSpinLock& lock_;
    };

    std::array<T, Capacity> buffer_{};
    std::size_t head_{0}, tail_{0}, count_{0};
    SPSCSpinLock lock_;
};
