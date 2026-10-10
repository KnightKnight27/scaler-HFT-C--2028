#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace hft {
struct Packet64 {
    std::array<std::uint64_t, 8> words{};
};
static_assert(sizeof(Packet64) == 64, "The benchmark payload must be 64 bytes");

class SpinMutex {
public:
    void lock() noexcept {
        unsigned probes = 0;
        for (;;) {
            // Poll with reads while busy; exchange only when it looks free.
            while (held_.load(std::memory_order_relaxed)) {
                if (++probes == 64) {
                    std::this_thread::yield();
                    probes = 0;
                }
            }
            if (!held_.exchange(true, std::memory_order_acquire)) return;
        }
    }
    void unlock() noexcept { held_.store(false, std::memory_order_release); }
private:
    std::atomic<bool> held_{false};
};

// Slots form two index-linked lists: available slots and queued packets.
// One producer and one consumer; all pool/list changes hold the same lock.
template <std::size_t Capacity, typename Mutex = std::mutex>
class BoundedSpscQueue {
    static_assert(Capacity > 0, "A queue needs at least one slot");
    static constexpr std::size_t none = Capacity;
    struct Slot {
        Packet64 packet;
        std::size_t next = none;
    };
public:
    BoundedSpscQueue() : slots_(std::make_unique<Slot[]>(Capacity)) {
        for (std::size_t i = 0; i < Capacity; ++i) slots_[i].next = i + 1;
    }
    BoundedSpscQueue(const BoundedSpscQueue&) = delete;
    BoundedSpscQueue& operator=(const BoundedSpscQueue&) = delete;

    bool try_push(const Packet64& packet) {
        std::lock_guard<Mutex> guard(mutex_);
        if (free_head_ == none) return false;
        const auto index = free_head_;
        free_head_ = slots_[index].next;
        slots_[index].packet = packet;
        slots_[index].next = none;
        if (queued_head_ == none) queued_head_ = index;
        else slots_[queued_tail_].next = index;
        queued_tail_ = index;
        return true;
    }

    bool try_pop(Packet64& packet) {
        std::lock_guard<Mutex> guard(mutex_);
        if (queued_head_ == none) return false;
        const auto index = queued_head_;
        packet = slots_[index].packet;
        queued_head_ = slots_[index].next;
        if (queued_head_ == none) queued_tail_ = none;
        slots_[index].next = free_head_;
        free_head_ = index;
        return true;
    }

private:
    // One allocation at construction; operations recycle slots without new/delete.
    std::unique_ptr<Slot[]> slots_;
    std::size_t free_head_ = 0;
    std::size_t queued_head_ = none;
    std::size_t queued_tail_ = none;
    Mutex mutex_;
};
} // namespace hft
