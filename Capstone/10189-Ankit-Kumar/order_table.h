// order_table.h - flat open-addressing hash table: order id -> where the order rests.
// Used by implementation C instead of std::unordered_map.
//
// All entries live in one array of 16-byte slots (4 per cache line); there is no node per
// order and no pointer to follow. qty == 0 marks an empty slot (a live order always has
// qty > 0), so every 64-bit id value is usable.
//
// Collisions: linear probing with Robin Hood insertion. An entry's "distance" is how far
// it sits from its home slot. On insert, if the new entry has travelled further than the
// entry in the slot, they swap and the old one moves on. This keeps every run of entries
// sorted by home slot, which gives two early exits:
//   - find stops as soon as it meets an entry closer to home than the probe has come
//   - erase (backward shift) stops at the first entry that is already in its home slot
// Without Robin Hood the erase has to scan to the end of the run, and with sequential ids
// a run can be hundreds of slots long (see "how C evolved" in the README).
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "common.h"

class OrderTable {
public:
    struct Entry {
        OrderId id;
        uint32_t qty;    // 0 = empty slot
        uint16_t level;  // index into the side's level array
        Side side;
    };
    static_assert(sizeof(Entry) == 16, "keep 4 entries per cache line");

    explicit OrderTable(size_t expected) { rebuild(capacity_for(expected)); }

    // false if the id is already in the table
    bool insert(OrderId id, uint32_t qty, uint16_t level, Side side) {
        if ((size_ + 1) * 4 > slots_.size() * 3) rebuild(slots_.size() * 2);  // keep load <= 75%
        if (find(id) != kNotFound) return false;
        place(Entry{id, qty, level, side});
        ++size_;
        return true;
    }

    // Finds the order, copies it to `out` and removes it. false if not found.
    bool take(OrderId id, Entry& out) {
        size_t hole = find(id);
        if (hole == kNotFound) return false;
        out = slots_[hole];
        // backward shift: slide the rest of the run back by one slot
        size_t next = (hole + 1) & mask_;
        while (slots_[next].qty != 0 && distance(next) != 0) {
            slots_[hole] = slots_[next];
            hole = next;
            next = (next + 1) & mask_;
        }
        slots_[hole].qty = 0;
        --size_;
        return true;
    }

    size_t size() const { return size_; }

private:
    static constexpr size_t kNotFound = ~size_t{0};

#ifdef ORDER_TABLE_FIBONACCI_HASH
    // Fibonacci hashing: multiply by 2^64 / golden ratio and keep the top bits.
    // Spreads ids evenly, but consecutive ids land on random cache lines.
    size_t home(OrderId id) const { return static_cast<size_t>((id * 0x9E3779B97F4A7C15ull) >> shift_); }
#else
    // XOR-fold the high bits of the id onto the low bits. Consecutive ids (which is how
    // ids are usually handed out) go to consecutive slots, so adds walk the table in
    // order instead of touching a random cache line each time. The folding keeps ids
    // that differ only in their high bits apart.
    // The README ("hash experiment") measures this against Fibonacci hashing.
    size_t home(OrderId id) const { return static_cast<size_t>(id ^ (id >> 21) ^ (id >> 42)) & mask_; }
#endif

    // how many slots entry i sits past its home slot
    size_t distance(size_t i) const { return (i - home(slots_[i].id)) & mask_; }

    size_t find(OrderId id) const {
        size_t i = home(id);
        for (size_t dist = 0;; ++dist, i = (i + 1) & mask_) {
            if (slots_[i].qty == 0) return kNotFound;
            if (slots_[i].id == id) return i;
            if (distance(i) < dist) return kNotFound;  // `id` would have taken this slot
        }
    }

    // Robin Hood insert of an id that is known not to be in the table.
    void place(Entry e) {
        size_t i = home(e.id);
        for (size_t dist = 0;; ++dist, i = (i + 1) & mask_) {
            if (slots_[i].qty == 0) {
                slots_[i] = e;
                return;
            }
            const size_t d = distance(i);
            if (d < dist) {  // the resident is closer to home than we are: take its slot
                std::swap(e, slots_[i]);
                dist = d;
            }
        }
    }

    static size_t capacity_for(size_t expected) {
        size_t cap = 16;
        while (cap < expected) cap <<= 1;
        return cap;
    }

    void rebuild(size_t new_cap) {
        std::vector<Entry> old;
        old.swap(slots_);
        slots_.assign(new_cap, Entry{0, 0, 0, Side::Buy});
        mask_ = new_cap - 1;
        shift_ = 64;
        for (size_t c = new_cap; c > 1; c >>= 1) --shift_;  // shift_ = 64 - log2(new_cap)
        for (const Entry& e : old)
            if (e.qty != 0) place(e);
    }

    std::vector<Entry> slots_;
    size_t mask_ = 0;
    int shift_ = 64;
    size_t size_ = 0;
};
