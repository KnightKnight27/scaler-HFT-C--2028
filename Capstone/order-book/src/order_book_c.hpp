// Implementation C (own design): price-indexed flat array of levels + occupancy
// bitmap + pooled orders in intrusive FIFO lists + open-addressing id index.
//
//  * levels_[tick - kMinTick] holds the level's total qty and the head/tail of
//    its order list. All 10,001 levels are allocated once up front.
//  * bits_ has one bit per level (set = non-empty). Finding the next best price
//    after the best level empties scans 64 levels per word with clz/ctz.
//  * Orders live in one contiguous pool (std::vector) and are linked by 32-bit
//    indices, not pointers. Freed slots go on a free list and are reused.
//  * IdIndex maps order id -> pool slot with linear probing (no per-entry
//    allocation, backward-shift deletion so there are no tombstones).
#pragma once

#include <array>
#include <cstddef>

#include "common.hpp"

namespace book_c_detail {

constexpr uint32_t kNil = UINT32_MAX;

// Open-addressing hash map uint64_t -> uint32_t. A slot is empty iff value == kNil.
class IdIndex {
public:
    explicit IdIndex(size_t initial_capacity = 1 << 16) { rehash(round_up_pow2(initial_capacity)); }

    // Returns false (and changes nothing) if the key is already present.
    bool insert(uint64_t key, uint32_t value) {
        if ((size_ + 1) * 2 > slots_.size()) rehash(slots_.size() * 2);
        size_t i = home(key);
        while (slots_[i].value != kNil) {
            if (slots_[i].key == key) return false;
            i = (i + 1) & mask_;
        }
        slots_[i] = Slot{key, value};
        ++size_;
        return true;
    }

    bool contains(uint64_t key) const { return find_slot(key) != kNotFound; }

    // Removes the key and returns its value, or kNil if absent.
    uint32_t take(uint64_t key) {
        size_t i = find_slot(key);
        if (i == kNotFound) return kNil;
        const uint32_t value = slots_[i].value;
        // Backward-shift deletion: pull later entries of the probe run into the hole.
        size_t j = i;
        while (true) {
            j = (j + 1) & mask_;
            if (slots_[j].value == kNil) break;
            const size_t k = home(slots_[j].key);
            const bool stays = (i <= j) ? (i < k && k <= j) : (i < k || k <= j);
            if (stays) continue;
            slots_[i] = slots_[j];
            i = j;
        }
        slots_[i].value = kNil;
        --size_;
        return value;
    }

private:
    struct Slot {
        uint64_t key;
        uint32_t value;
    };
    static constexpr size_t kNotFound = SIZE_MAX;

    std::vector<Slot> slots_;
    size_t mask_ = 0;
    int shift_ = 0;
    size_t size_ = 0;

    static size_t round_up_pow2(size_t n) {
        size_t p = 16;
        while (p < n) p <<= 1;
        return p;
    }

    // Fibonacci hashing: spreads sequential ids across the table.
    size_t home(uint64_t key) const { return (key * 0x9E3779B97F4A7C15ull) >> shift_; }

    size_t find_slot(uint64_t key) const {
        size_t i = home(key);
        while (slots_[i].value != kNil) {
            if (slots_[i].key == key) return i;
            i = (i + 1) & mask_;
        }
        return kNotFound;
    }

    void rehash(size_t capacity) {
        std::vector<Slot> old;
        old.swap(slots_);
        slots_.assign(capacity, Slot{0, kNil});
        mask_ = capacity - 1;
        shift_ = 64 - __builtin_ctzll(capacity);
        size_ = 0;
        for (const Slot& s : old)
            if (s.value != kNil) insert(s.key, s.value);
    }
};

}  // namespace book_c_detail

class OrderBookC {
public:
    explicit OrderBookC(size_t expected_orders = 1 << 16) : index_(expected_orders * 2) {
        levels_.assign(kLevels, PriceLevel{});
        bits_.fill(0);
        pool_.reserve(expected_orders);
    }

    void add_order(uint64_t id, Side side, double price, long qty) {
        book::Tick tick;
        if (!book::to_valid_tick(side, price, qty, tick)) return;
        const uint32_t slot = allocate();
        if (!index_.insert(id, slot)) {  // duplicate id
            release(slot);
            return;
        }

        const int li = tick - book::kMinTick;
        PriceLevel& level = levels_[li];
        pool_[slot] = Order{id, qty, level.tail, kNil, li};
        if (level.tail != kNil)
            pool_[level.tail].next = slot;
        else
            level.head = slot;
        level.tail = slot;
        if (level.total == 0) set_bit(li);
        level.total += qty;

        if (side == Side::Buy) {
            if (li > best_bid_) best_bid_ = li;
        } else {
            if (best_ask_ < 0 || li < best_ask_) best_ask_ = li;
        }
    }

    bool cancel_order(uint64_t id) {
        const uint32_t slot = index_.take(id);
        if (slot == kNil) return false;

        const Order& o = pool_[slot];
        const int li = o.level;
        PriceLevel& level = levels_[li];
        if (o.prev != kNil) pool_[o.prev].next = o.next; else level.head = o.next;
        if (o.next != kNil) pool_[o.next].prev = o.prev; else level.tail = o.prev;
        level.total -= o.qty;
        release(slot);

        if (level.total == 0) {
            clear_bit(li);
            if (li == best_bid_) best_bid_ = prev_set(li - 1);
            else if (li == best_ask_) best_ask_ = next_set(li + 1);
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (best_bid_ >= 0) {
            b.has_bid = true;
            b.bid_price = book::to_price(best_bid_ + book::kMinTick);
            b.bid_qty = levels_[best_bid_].total;
        }
        if (best_ask_ >= 0) {
            b.has_ask = true;
            b.ask_price = book::to_price(best_ask_ + book::kMinTick);
            b.ask_qty = levels_[best_ask_].total;
        }
        return b;
    }

    // Bids and asks occupy disjoint index ranges (asks are all above bids), so
    // scanning down from the best bid never reaches an ask and vice versa.
    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(depth);
        for (int li = best_bid_; li >= 0 && (int)out.size() < depth; li = prev_set(li - 1))
            out.push_back(level_at(li));
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(depth);
        for (int li = best_ask_; li >= 0 && (int)out.size() < depth; li = next_set(li + 1))
            out.push_back(level_at(li));
        return out;
    }

private:
    static constexpr uint32_t kNil = book_c_detail::kNil;
    static constexpr int kLevels = book::kMaxTick - book::kMinTick + 1;  // 10,001
    static constexpr int kWords = (kLevels + 63) / 64;

    struct Order {
        uint64_t id;
        long qty;
        uint32_t prev;  // pool index, kNil if none; reused as free-list link
        uint32_t next;
        int level;      // index into levels_
    };

    struct PriceLevel {
        long total = 0;
        uint32_t head = kNil;
        uint32_t tail = kNil;
    };

    std::vector<PriceLevel> levels_;
    std::array<uint64_t, kWords> bits_;
    std::vector<Order> pool_;
    uint32_t free_head_ = kNil;
    book_c_detail::IdIndex index_;
    int best_bid_ = -1;  // level index, -1 = no bids
    int best_ask_ = -1;  // level index, -1 = no asks

    Level level_at(int li) const { return Level{book::to_price(li + book::kMinTick), levels_[li].total}; }

    uint32_t allocate() {
        if (free_head_ != kNil) {
            const uint32_t slot = free_head_;
            free_head_ = pool_[slot].next;
            return slot;
        }
        pool_.emplace_back();
        return static_cast<uint32_t>(pool_.size() - 1);
    }

    void release(uint32_t slot) {
        pool_[slot].next = free_head_;
        free_head_ = slot;
    }

    void set_bit(int li) { bits_[li >> 6] |= 1ull << (li & 63); }
    void clear_bit(int li) { bits_[li >> 6] &= ~(1ull << (li & 63)); }

    // Highest non-empty level index <= from, or -1.
    int prev_set(int from) const {
        if (from < 0) return -1;
        int w = from >> 6;
        uint64_t word = bits_[w] & (~0ull >> (63 - (from & 63)));
        while (true) {
            if (word) return (w << 6) + 63 - __builtin_clzll(word);
            if (--w < 0) return -1;
            word = bits_[w];
        }
    }

    // Lowest non-empty level index >= from, or -1.
    int next_set(int from) const {
        if (from >= kLevels) return -1;
        int w = from >> 6;
        uint64_t word = bits_[w] & (~0ull << (from & 63));
        while (true) {
            if (word) return (w << 6) + __builtin_ctzll(word);
            if (++w >= kWords) return -1;
            word = bits_[w];
        }
    }
};
