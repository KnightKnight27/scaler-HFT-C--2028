#pragma once

#include <cstring>
#include <vector>

#include "common.hpp"

// Two-level bitmap over the 5000 price slots of one side. A set bit means the
// level has qty > 0. Level 0 has one bit per price, level 1 has one bit per
// level-0 word, so any "next non-empty price" query touches at most a few words.
class PriceBitmap {
public:
    void set(int i) {
        l0_[i >> 6] |= 1ULL << (i & 63);
        l1_[i >> 12] |= 1ULL << ((i >> 6) & 63);
    }

    void clear(int i) {
        l0_[i >> 6] &= ~(1ULL << (i & 63));
        if (l0_[i >> 6] == 0) l1_[i >> 12] &= ~(1ULL << ((i >> 6) & 63));
    }

    // first set bit >= i, or -1
    int next_at_or_above(int i) const {
        if (i >= N) return -1;
        int w = i >> 6;
        uint64_t m = l0_[w] & (~0ULL << (i & 63));
        if (m) return (w << 6) + __builtin_ctzll(m);
        int w1 = w + 1;
        if (w1 >= W0) return -1;
        int s = w1 >> 6;
        uint64_t m1 = l1_[s] & (~0ULL << (w1 & 63));
        for (;;) {
            if (m1) {
                int ww = (s << 6) + __builtin_ctzll(m1);
                return (ww << 6) + __builtin_ctzll(l0_[ww]);
            }
            if (++s >= W1) return -1;
            m1 = l1_[s];
        }
    }

    // last set bit <= i, or -1
    int next_at_or_below(int i) const {
        if (i < 0) return -1;
        int w = i >> 6;
        uint64_t m = l0_[w] & (~0ULL >> (63 - (i & 63)));
        if (m) return (w << 6) + 63 - __builtin_clzll(m);
        int w1 = w - 1;
        if (w1 < 0) return -1;
        int s = w1 >> 6;
        uint64_t m1 = l1_[s] & (~0ULL >> (63 - (w1 & 63)));
        for (;;) {
            if (m1) {
                int ww = (s << 6) + 63 - __builtin_clzll(m1);
                return (ww << 6) + 63 - __builtin_clzll(l0_[ww]);
            }
            if (--s < 0) return -1;
            m1 = l1_[s];
        }
    }

    int lowest() const { return next_at_or_above(0); }
    int highest() const { return next_at_or_below(N - 1); }

private:
    static constexpr int N = LEVELS_PER_SIDE;
    static constexpr int W0 = (N + 63) / 64;
    static constexpr int W1 = (W0 + 63) / 64;
    uint64_t l0_[W0] = {};
    uint64_t l1_[W1] = {};
};

// Implementation C: my own design, no node allocations on the hot path.
//   levels: one flat long[5000] per side, indexed by (price - band start),
//           plus a PriceBitmap per side to find the best non-empty level
//   orders: open-addressing hash table (linear probing, backward-shift delete)
//           in one flat array of slots, so it works with any 64-bit order id
class ArrayBook {
public:
    ArrayBook() : slots_(1u << 16), mask_(slots_.size() - 1) {}

    void add_order(uint64_t id, Side side, Price price, long qty) {
        if (!valid_order(side, price, qty)) return;
        if (size_ * 2 >= slots_.size()) grow();
        size_t i = home(id);
        while (slots_[i].qty != 0) {
            if (slots_[i].id == id) return;  // id already live
            i = (i + 1) & mask_;
        }
        slots_[i] = Slot{id, qty, price, side};
        size_++;

        int idx = index_of(side, price);
        long& q = (side == Side::Buy ? bid_qty_ : ask_qty_)[idx];
        if (q == 0) (side == Side::Buy ? bid_bits_ : ask_bits_).set(idx);
        q += qty;
    }

    bool cancel_order(uint64_t id) {
        size_t i = home(id);
        while (slots_[i].qty != 0) {
            if (slots_[i].id == id) {
                const Slot s = slots_[i];
                int idx = index_of(s.side, s.price);
                long& q = (s.side == Side::Buy ? bid_qty_ : ask_qty_)[idx];
                q -= s.qty;
                if (q == 0) (s.side == Side::Buy ? bid_bits_ : ask_bits_).clear(idx);
                erase_at(i);
                size_--;
                return true;
            }
            i = (i + 1) & mask_;
        }
        return false;
    }

    BBO get_bbo() const {
        BBO b;
        int bi = bid_bits_.highest();
        if (bi >= 0) {
            b.has_bid = true;
            b.bid_price = BID_LOW + bi;
            b.bid_qty = bid_qty_[bi];
        }
        int ai = ask_bits_.lowest();
        if (ai >= 0) {
            b.has_ask = true;
            b.ask_price = ASK_LOW + ai;
            b.ask_qty = ask_qty_[ai];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (int i = bid_bits_.highest(); i >= 0 && (int)out.size() < depth;
             i = bid_bits_.next_at_or_below(i - 1))
            out.push_back({BID_LOW + i, bid_qty_[i]});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (int i = ask_bits_.lowest(); i >= 0 && (int)out.size() < depth;
             i = ask_bits_.next_at_or_above(i + 1))
            out.push_back({ASK_LOW + i, ask_qty_[i]});
        return out;
    }

private:
    // A slot is empty when qty == 0 (live orders always have qty > 0).
    struct Slot {
        uint64_t id = 0;
        long qty = 0;
        Price price = 0;
        Side side = Side::Buy;
    };

    static int index_of(Side side, Price price) {
        return side == Side::Buy ? price - BID_LOW : price - ASK_LOW;
    }

    size_t home(uint64_t id) const {
        return (size_t)((id * 0x9E3779B97F4A7C15ULL) >> 20) & mask_;
    }

    // Backward-shift deletion keeps probe chains unbroken without tombstones.
    void erase_at(size_t i) {
        size_t j = i;
        for (;;) {
            j = (j + 1) & mask_;
            if (slots_[j].qty == 0) break;
            size_t h = home(slots_[j].id);
            bool stays = (i <= j) ? (i < h && h <= j) : (i < h || h <= j);
            if (stays) continue;
            slots_[i] = slots_[j];
            i = j;
        }
        slots_[i].qty = 0;
    }

    void grow() {
        std::vector<Slot> old(slots_.size() * 2);
        old.swap(slots_);
        mask_ = slots_.size() - 1;
        for (const Slot& s : old) {
            if (s.qty == 0) continue;
            size_t i = home(s.id);
            while (slots_[i].qty != 0) i = (i + 1) & mask_;
            slots_[i] = s;
        }
    }

    long bid_qty_[LEVELS_PER_SIDE] = {};
    long ask_qty_[LEVELS_PER_SIDE] = {};
    PriceBitmap bid_bits_;
    PriceBitmap ask_bits_;
    std::vector<Slot> slots_;
    size_t mask_;
    size_t size_ = 0;
};
