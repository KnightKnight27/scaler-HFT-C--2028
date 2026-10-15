// book_c.hpp - Implementation C (my own design): price-indexed arrays + bitmap + custom hash.
//
//   price levels : fixed arrays, ONE SLOT PER TICK. The tick IS the array index, so there is no
//                  searching and no per-level allocation. 5000 slots x 8 B = 40 KB per side
//                  (fits in L1/L2).
//   non-empty map: a bitmap (1 bit per tick, 79 x uint64) says which levels hold orders.
//   best price   : cached best_bid_/best_ask_ index, updated in O(1) on add. When the best level
//                  empties we find the next one by scanning the bitmap 64 levels per instruction
//                  (clz/ctz) instead of walking a tree.
//   orders       : custom open-addressing hash table (linear probing, backward-shift delete, no
//                  tombstones, one flat pre-allocated array, no per-order heap nodes).
//   cancel lookup: one hash probe -> (tick, side, qty), then direct array index.
//
//   add_order    O(1)  (+ rare rehash if the table passes 75% load)
//   cancel_order O(1)  typical; O(W) = at most 79 words to find a new best when the best empties
//   get_bbo      O(1)
//   get_bids/asks O(depth + words scanned)
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

#include "common.hpp"

class BookC {
public:
    explicit BookC(std::size_t initial_slots = std::size_t{1} << 20) { init_table(initial_slots); }

    bool add_order(OrderId id, Side side, Price px, long qty) {
        if (qty <= 0 || !valid_price(side, px)) return false;
        if ((size_ + 1) * 4 > slots_.size() * 3) grow();  // keep load factor <= 0.75

        std::size_t i = home(id);
        while (slots_[i].used) {
            if (slots_[i].id == id) return false;  // duplicate id
            i = (i + 1) & mask_;
        }
        const bool buy = (side == Side::Buy);
        const int  idx = buy ? px - kBidLoTick : px - kAskLoTick;
        slots_[i] = Slot{id, qty, idx, buy, true};
        ++size_;

        if (buy) {
            bid_qty_[idx] += qty;
            set_bit(bid_bits_, idx);
            if (idx > best_bid_) best_bid_ = idx;  // higher is better for bids
        } else {
            ask_qty_[idx] += qty;
            set_bit(ask_bits_, idx);
            if (best_ask_ < 0 || idx < best_ask_) best_ask_ = idx;  // lower is better for asks
        }
        return true;
    }

    bool cancel_order(OrderId id) {
        std::size_t i = home(id);
        while (true) {
            if (!slots_[i].used) return false;  // unknown or already cancelled
            if (slots_[i].id == id) break;
            i = (i + 1) & mask_;
        }
        const Slot s = slots_[i];
        erase_slot(i);
        --size_;

        if (s.buy) {
            if ((bid_qty_[s.idx] -= s.qty) == 0) {
                clear_bit(bid_bits_, s.idx);
                if (s.idx == best_bid_) best_bid_ = next_lower(bid_bits_, s.idx);
            }
        } else {
            if ((ask_qty_[s.idx] -= s.qty) == 0) {
                clear_bit(ask_bits_, s.idx);
                if (s.idx == best_ask_) best_ask_ = next_higher(ask_bits_, s.idx);
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
        if (best_bid_ >= 0) {
            b.has_bid   = true;
            b.bid_price = best_bid_ + kBidLoTick;
            b.bid_qty   = bid_qty_[best_bid_];
        }
        if (best_ask_ >= 0) {
            b.has_ask   = true;
            b.ask_price = best_ask_ + kAskLoTick;
            b.ask_qty   = ask_qty_[best_ask_];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(static_cast<std::size_t>(std::min(depth, kLevelsPerSide)));  // no regrowth
        for (int i = best_bid_; i >= 0 && static_cast<int>(out.size()) < depth;
             i = next_lower(bid_bits_, i))
            out.push_back(Level{i + kBidLoTick, bid_qty_[i]});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(static_cast<std::size_t>(std::min(depth, kLevelsPerSide)));
        for (int i = best_ask_; i >= 0 && static_cast<int>(out.size()) < depth;
             i = next_higher(ask_bits_, i))
            out.push_back(Level{i + kAskLoTick, ask_qty_[i]});
        return out;
    }

private:
    static constexpr int kWords = (kLevelsPerSide + 63) / 64;  // 79

    struct Slot {
        OrderId id;
        long    qty;
        int     idx;   // tick index inside its side's array
        bool    buy;
        bool    used;
    };

    // ---- bitmap helpers -------------------------------------------------------------------
    static void set_bit(std::uint64_t* bits, int i)   { bits[i >> 6] |=  (1ULL << (i & 63)); }
    static void clear_bit(std::uint64_t* bits, int i) { bits[i >> 6] &= ~(1ULL << (i & 63)); }

    // Largest set index strictly below idx, or -1.
    static int next_lower(const std::uint64_t* bits, int idx) {
        int w = idx >> 6;
        std::uint64_t m = bits[w] & ((1ULL << (idx & 63)) - 1);
        while (true) {
            if (m) return (w << 6) + 63 - __builtin_clzll(m);
            if (--w < 0) return -1;
            m = bits[w];
        }
    }

    // Smallest set index strictly above idx, or -1.
    static int next_higher(const std::uint64_t* bits, int idx) {
        int w = idx >> 6;
        std::uint64_t m = bits[w] & ~((2ULL << (idx & 63)) - 1);
        while (true) {
            if (m) return (w << 6) + __builtin_ctzll(m);
            if (++w >= kWords) return -1;
            m = bits[w];
        }
    }

    // ---- hash table -----------------------------------------------------------------------
    void init_table(std::size_t slots) {
        std::size_t cap = 16;
        while (cap < slots) cap <<= 1;  // power of two
        slots_.assign(cap, Slot{});
        mask_ = cap - 1;
        shift_ = 64;
        for (std::size_t c = cap; c > 1; c >>= 1) --shift_;  // shift_ = 64 - log2(cap)
        size_ = 0;
    }

    // Fibonacci hashing: multiply by 2^64/phi, keep the top bits.
    std::size_t home(OrderId id) const {
        return static_cast<std::size_t>((id * 0x9E3779B97F4A7C15ULL) >> shift_);
    }

    // Backward-shift deletion for linear probing: no tombstones needed.
    void erase_slot(std::size_t i) {
        std::size_t j = i;
        while (true) {
            j = (j + 1) & mask_;
            if (!slots_[j].used) break;
            const std::size_t k = home(slots_[j].id);  // where slot j would like to live
            // Slot j may move into the hole at i only if its home k is NOT cyclically in (i, j].
            const bool stays = (i <= j) ? (i < k && k <= j) : (i < k || k <= j);
            if (!stays) {
                slots_[i] = slots_[j];
                i = j;
            }
        }
        slots_[i].used = false;
    }

    void grow() {
        std::vector<Slot> old;
        old.swap(slots_);
        init_table(old.size() * 2);
        for (const Slot& s : old) {
            if (!s.used) continue;
            std::size_t i = home(s.id);
            while (slots_[i].used) i = (i + 1) & mask_;
            slots_[i] = s;
            ++size_;
        }
    }

    long          bid_qty_[kLevelsPerSide] = {};
    long          ask_qty_[kLevelsPerSide] = {};
    std::uint64_t bid_bits_[kWords] = {};
    std::uint64_t ask_bits_[kWords] = {};
    int           best_bid_ = -1;  // index of best bid, -1 if none
    int           best_ask_ = -1;

    std::vector<Slot> slots_;
    std::size_t       mask_ = 0;
    std::size_t       size_ = 0;
    int               shift_ = 0;
};
