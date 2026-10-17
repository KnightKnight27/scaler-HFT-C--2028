#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>
#include <vector>

#include "common.hpp"

// Align storage to 64 bytes so four 16-byte slots share a cache line.
template <class T>
struct CacheLineAllocator {
    using value_type = T;

    CacheLineAllocator() = default;
    template <class U>
    CacheLineAllocator(const CacheLineAllocator<U>&) {}

    T* allocate(std::size_t n) { return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t{64})); }
    void deallocate(T* p, std::size_t) { ::operator delete(p, std::align_val_t{64}); }
};

template <class T, class U>
bool operator==(const CacheLineAllocator<T>&, const CacheLineAllocator<U>&) {
    return true;
}

template <class T, class U>
bool operator!=(const CacheLineAllocator<T>&, const CacheLineAllocator<U>&) {
    return false;
}

// Maps an order id to its tick and quantity using linear probing.
// Keep the table at most half full and move entries back after an erase.
class OrderTable {
public:
    struct Slot {
        std::uint64_t id;
        Tick tick;
        std::int32_t qty;  // 0 marks an empty slot; a live order always has qty >= 1
    };

    explicit OrderTable(std::size_t expected_orders = 0) {
        std::size_t capacity = 16;
        int bits = 4;
        while (capacity < 2 * expected_orders) {
            capacity *= 2;
            ++bits;
        }
        slots_.assign(capacity, Slot{});
        mask_ = capacity - 1;
        line_shift_ = 64 - (bits - 2);  // the table has capacity / 4 cache lines
    }

    // Returns the slot holding `id`, or nullptr if the id is not in the table.
    Slot* find(std::uint64_t id) {
        for (std::size_t i = home(id);; i = (i + 1) & mask_) {
            Slot& slot = slots_[i];
            if (slot.qty == 0) return nullptr;
            if (slot.id == id) return &slot;
        }
    }

    // Returns false, and changes nothing, if `id` is already in the table.
    bool insert(std::uint64_t id, Tick tick, std::int32_t qty) {
        if (2 * (size_ + 1) > slots_.size()) grow();
        for (std::size_t i = home(id);; i = (i + 1) & mask_) {
            Slot& slot = slots_[i];
            if (slot.qty == 0) {
                slot = Slot{id, tick, qty};
                ++size_;
                return true;
            }
            if (slot.id == id) return false;
        }
    }

    // Removes the entry in `slot`, which must come from find().
    void erase(Slot* slot) {
        std::size_t hole = static_cast<std::size_t>(slot - slots_.data());
        for (std::size_t i = (hole + 1) & mask_; slots_[i].qty != 0; i = (i + 1) & mask_) {
            // The entry at i can stay only if its home slot lies after the hole,
            // in (hole, i]. Otherwise its lookup walks through the hole, so it
            // must move back into it, and its old slot becomes the new hole.
            const std::size_t h = home(slots_[i].id);
            const bool stays = hole < i ? (hole < h && h <= i) : (hole < h || h <= i);
            if (!stays) {
                slots_[hole] = slots_[i];
                hole = i;
            }
        }
        slots_[hole].qty = 0;
        --size_;
    }

    std::size_t size() const { return size_; }

private:
    using Slots = std::vector<Slot, CacheLineAllocator<Slot>>;

    // Low two bits choose a slot within a cache line; hash the other bits to
    // choose the line. Consecutive ids can then share the same cache line.
    std::size_t home(std::uint64_t id) const {
        const auto line = static_cast<std::size_t>(((id >> 2) * 0x9E3779B97F4A7C15ull) >> line_shift_);
        return (line << 2) | static_cast<std::size_t>(id & 3);
    }

    // Doubles the capacity and re-inserts every entry. Only runs when the
    // constructor's size hint was too small.
    void grow() {
        Slots old = std::move(slots_);
        slots_.assign(2 * old.size(), Slot{});
        mask_ = slots_.size() - 1;
        --line_shift_;
        for (const Slot& slot : old) {
            if (slot.qty == 0) continue;
            std::size_t i = home(slot.id);
            while (slots_[i].qty != 0) i = (i + 1) & mask_;
            slots_[i] = slot;
        }
    }

    Slots slots_;
    std::size_t mask_ = 0;  // capacity - 1 (capacity is a power of two)
    int line_shift_ = 0;    // 64 - log2(number of cache lines)
    std::size_t size_ = 0;
};
