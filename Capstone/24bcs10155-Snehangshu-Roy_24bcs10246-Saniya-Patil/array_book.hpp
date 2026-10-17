#pragma once

// Implementation C: our own design.
//
//   price levels -> one flat array of qty indexed by (tick - 5000). The whole
//                   band is only 10001 ticks so we just allocate all of it.
//   best price   -> cached best_bid_ / best_ask_ index. A bitmap (1 bit per
//                   level) tells us which levels are non-empty, so when the
//                   best level empties we find the next one by scanning 64
//                   levels per word instead of one at a time.
//   orders       -> our own open addressing hash table (OrderTable below),
//                   one flat array of 16 byte slots, linear probing.

#include <cstring>
#include <vector>

#include "common.hpp"

// Starting number of slots in the order table (it doubles when half full).
// Only a macro so the pre-sizing experiment in REPORT.md can be rebuilt with
// -DORDER_TABLE_INIT_CAP=2097152 without editing the code.
#ifndef ORDER_TABLE_INIT_CAP
#define ORDER_TABLE_INIT_CAP (1 << 16)
#endif

// id -> (tick, qty). Linear probing, power of two capacity, max load 0.5.
// Deletion uses backward shift instead of tombstones so lookups never have
// to walk over dead slots.
class OrderTable {
public:
    struct Slot {
        uint64_t id;
        int32_t tick;  // 0 = empty slot (real ticks are >= 5000)
        int32_t qty;
    };

    explicit OrderTable(size_t initial_capacity = ORDER_TABLE_INIT_CAP) { init(initial_capacity); }

    // returns false if the id is already in the table
    bool insert(uint64_t id, int tick, int qty) {
        if ((count_ + 1) * 2 > slots_.size()) grow();
        size_t i = home(id);
        while (slots_[i].tick != 0) {
            if (slots_[i].id == id) return false;
            i = (i + 1) & mask_;
        }
        slots_[i] = Slot{id, tick, qty};
        ++count_;
        return true;
    }

    // returns the slot holding id, or nullptr
    Slot* find(uint64_t id) {
        size_t i = home(id);
        while (slots_[i].tick != 0) {
            if (slots_[i].id == id) return &slots_[i];
            i = (i + 1) & mask_;
        }
        return nullptr;
    }

    void erase(Slot* s) {
        size_t i = s - slots_.data();
        size_t j = i;
        // shift back any entry after the hole that would otherwise become
        // unreachable (its home is at or before the hole)
        while (true) {
            j = (j + 1) & mask_;
            if (slots_[j].tick == 0) break;
            size_t h = home(slots_[j].id);
            if (((j - h) & mask_) >= ((j - i) & mask_)) {
                slots_[i] = slots_[j];
                i = j;
            }
        }
        slots_[i].tick = 0;
        --count_;
    }

    size_t size() const { return count_; }

private:
    std::vector<Slot> slots_;
    size_t mask_ = 0;
    int shift_ = 0;
    size_t count_ = 0;

    void init(size_t cap) {
        size_t c = 16;
        while (c < cap) c <<= 1;
        slots_.assign(c, Slot{0, 0, 0});
        mask_ = c - 1;
        shift_ = 64 - __builtin_ctzll(c);
        count_ = 0;
    }

    // fibonacci hashing, top bits of id * golden ratio
    size_t home(uint64_t id) const { return (id * 0x9E3779B97F4A7C15ULL) >> shift_; }

    void grow() {
        std::vector<Slot> old;
        old.swap(slots_);
        init(old.size() * 2);
        for (const Slot& s : old)
            if (s.tick != 0) insert(s.id, s.tick, s.qty);
    }
};

class ArrayBook {
public:
    ArrayBook() {
        std::memset(qty_, 0, sizeof(qty_));
        std::memset(used_, 0, sizeof(used_));
    }

    bool add_order(uint64_t id, Side side, double price, long qty) {
        int tick;
        if (!valid_qty(qty) || !price_to_tick(side, price, tick)) return false;
        if (!orders_.insert(id, tick, static_cast<int>(qty))) return false;

        int i = tick - BASE;
        if (qty_[i] == 0) set_bit(i);
        qty_[i] += qty;

        if (side == Side::Buy) {
            if (i > best_bid_) best_bid_ = i;
        } else {
            if (i < best_ask_) best_ask_ = i;
        }
        return true;
    }

    bool cancel_order(uint64_t id) {
        OrderTable::Slot* s = orders_.find(id);
        if (!s) return false;

        int i = s->tick - BASE;
        qty_[i] -= s->qty;
        orders_.erase(s);

        if (qty_[i] == 0) {
            clear_bit(i);
            // only need to search if we just emptied the best level
            if (i == best_bid_)
                best_bid_ = prev_used(i);
            else if (i == best_ask_)
                best_ask_ = next_used(i);
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (best_bid_ >= 0) {
            b.has_bid = true;
            b.bid_price = to_price(best_bid_ + BASE);
            b.bid_qty = qty_[best_bid_];
        }
        if (best_ask_ < N) {
            b.has_ask = true;
            b.ask_price = to_price(best_ask_ + BASE);
            b.ask_qty = qty_[best_ask_];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(depth < 64 ? depth : 64);
        for (int i = best_bid_; i >= 0 && (int)out.size() < depth; i = prev_used(i - 1))
            out.push_back({to_price(i + BASE), qty_[i]});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(depth < 64 ? depth : 64);
        for (int i = best_ask_; i < N && (int)out.size() < depth; i = next_used(i + 1))
            out.push_back({to_price(i + BASE), qty_[i]});
        return out;
    }

    size_t order_count() const { return orders_.size(); }

private:
    static constexpr int BASE = MIN_BID_TICK;
    static constexpr int N = MAX_ASK_TICK - MIN_BID_TICK + 1;  // 10001, index of 100.00 is never used
    static constexpr int WORDS = (N + 63) / 64;

    long qty_[N];
    uint64_t used_[WORDS];
    int best_bid_ = -1;  // -1 = no bids
    int best_ask_ = N;   // N  = no asks
    OrderTable orders_;

    void set_bit(int i) { used_[i >> 6] |= 1ULL << (i & 63); }
    void clear_bit(int i) { used_[i >> 6] &= ~(1ULL << (i & 63)); }

    // highest non-empty level <= i, or -1. bids are all below the mid index
    // so walking down from a bid never runs into an ask
    int prev_used(int i) const {
        if (i < 0) return -1;
        int w = i >> 6;
        uint64_t bits = used_[w] & (~0ULL >> (63 - (i & 63)));
        while (true) {
            if (bits) return (w << 6) + 63 - __builtin_clzll(bits);
            if (--w < 0) return -1;
            bits = used_[w];
        }
    }

    // lowest non-empty level >= i, or N
    int next_used(int i) const {
        if (i >= N) return N;
        int w = i >> 6;
        uint64_t bits = used_[w] & (~0ULL << (i & 63));
        while (true) {
            if (bits) return (w << 6) + __builtin_ctzll(bits);
            if (++w >= WORDS) return N;
            bits = used_[w];
        }
    }
};
