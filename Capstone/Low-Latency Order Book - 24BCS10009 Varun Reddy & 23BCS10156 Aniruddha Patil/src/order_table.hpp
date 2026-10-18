// Open-addressing hash table (linear probing) mapping order id -> resting order.
//
// * One flat array of 16-byte slots, so 4 slots share a cache line and a lookup
//   is usually a single cache-line access (no pointer chasing, no per-order malloc).
// * Capacity is a power of two and pre-sized up front; it doubles only if the load
//   factor would exceed 0.7.
// * Fibonacci (multiplicative) hashing spreads sequential ids evenly over the table.
// * Deletion uses backward-shift instead of tombstones, so probe chains never
//   degrade over time.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct OrderSlot {
    uint64_t id;
    int32_t qty;        // fits because add_order rejects qty > INT32_MAX
    uint16_t tick_idx;  // tick - px::kMinTick
    uint8_t is_buy;
    uint8_t used;
};
static_assert(sizeof(OrderSlot) == 16, "OrderSlot should be 16 bytes");

class OrderTable {
public:
    explicit OrderTable(size_t expected_orders) { rebuild(capacity_for(expected_orders)); }

    size_t size() const { return size_; }

    // Returns false if the id is already present.
    bool insert(const OrderSlot& s) {
        if ((size_ + 1) * 10 > slots_.size() * 7) rebuild(slots_.size() * 2);
        size_t i = home(s.id);
        while (slots_[i].used) {
            if (slots_[i].id == s.id) return false;
            i = (i + 1) & mask_;
        }
        slots_[i] = s;
        slots_[i].used = 1;
        ++size_;
        return true;
    }

    // Removes `id`, copying the removed order into `out`. Returns false if absent.
    bool erase(uint64_t id, OrderSlot& out) {
        size_t i = home(id);
        while (slots_[i].used) {
            if (slots_[i].id == id) {
                out = slots_[i];
                erase_at(i);
                --size_;
                return true;
            }
            i = (i + 1) & mask_;
        }
        return false;
    }

private:
    static size_t capacity_for(size_t n) {
        size_t cap = 16;
        while (cap * 7 < n * 10) cap *= 2;
        return cap;
    }

    size_t home(uint64_t id) const { return static_cast<size_t>((id * 0x9E3779B97F4A7C15ull) >> shift_); }

    // Backward-shift deletion: walk the cluster after slot i and pull back every
    // entry whose probe path passes through the hole, then free the final hole.
    void erase_at(size_t i) {
        size_t j = i;
        for (;;) {
            j = (j + 1) & mask_;
            if (!slots_[j].used) break;
            const size_t h = home(slots_[j].id);
            if (((j - h) & mask_) >= ((j - i) & mask_)) {  // hole i lies on j's probe path
                slots_[i] = slots_[j];
                i = j;
            }
        }
        slots_[i].used = 0;
    }

    void rebuild(size_t new_cap) {
        std::vector<OrderSlot> old;
        old.swap(slots_);
        slots_.assign(new_cap, OrderSlot{});
        mask_ = new_cap - 1;
        shift_ = 64 - static_cast<unsigned>(__builtin_ctzll(new_cap));
        size_ = 0;
        for (const OrderSlot& s : old)
            if (s.used) insert(s);
    }

    std::vector<OrderSlot> slots_;
    size_t mask_ = 0;
    unsigned shift_ = 64;
    size_t size_ = 0;
};
