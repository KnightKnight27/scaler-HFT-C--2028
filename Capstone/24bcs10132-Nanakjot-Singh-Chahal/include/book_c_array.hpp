// Implementation C (own design): flat price-indexed arrays + occupancy bitmap
// + a hand-written open-addressing hash table for order ids.
//
//   levels : long qty_[10001]           one slot per tick, index = tick - 5000
//   bitmap : uint64_t bits_[157]        bit i set <=> qty_[i] > 0
//   best   : cached best_bid_ / best_ask_ index, repaired with a bitmap scan
//            (count-leading/trailing-zeros) only when the best level empties
//   orders : OrderMap - linear probing, power-of-two capacity, Fibonacci
//            hashing, backward-shift delete (no tombstones), all in one
//            contiguous array.
#pragma once

#include <array>
#include <cstring>
#include <memory>

#include "common.hpp"

// Open-addressing hash map: uint64 id -> OrderInfo.
class OrderMap {
public:
    explicit OrderMap(size_t initial_capacity = 1u << 20) { alloc(round_pow2(initial_capacity)); }

    // Returns false if id already present.
    bool insert(uint64_t id, const OrderInfo& info) {
        if ((size_ + 1) * 2 > cap_) grow();  // keep load factor <= 0.5
        size_t i = home(id);
        while (slots_[i].used) {
            if (slots_[i].id == id) return false;
            i = (i + 1) & mask_;
        }
        slots_[i] = Slot{id, info.tick, info.side, true, info.qty};
        ++size_;
        return true;
    }

    // Removes id and writes its info to `out`. Returns false if not found.
    bool erase(uint64_t id, OrderInfo& out) {
        size_t i = home(id);
        while (true) {
            if (!slots_[i].used) return false;
            if (slots_[i].id == id) break;
            i = (i + 1) & mask_;
        }
        out = OrderInfo{slots_[i].tick, slots_[i].side, slots_[i].qty};
        // Backward-shift deletion: pull later entries of the same probe run
        // back into the hole so lookups never need tombstones.
        size_t hole = i;
        size_t j = i;
        while (true) {
            j = (j + 1) & mask_;
            if (!slots_[j].used) break;
            const size_t k = home(slots_[j].id);
            // Entry at j may move to `hole` only if its home is not in (hole, j].
            const bool in_range = hole <= j ? (hole < k && k <= j) : (hole < k || k <= j);
            if (!in_range) {
                slots_[hole] = slots_[j];
                hole = j;
            }
        }
        slots_[hole].used = false;
        --size_;
        return true;
    }

private:
    struct Slot {
        uint64_t id;
        int tick;
        Side side;
        bool used;
        long qty;
    };  // 32 bytes with padding

    static size_t round_pow2(size_t n) {
        size_t c = 16;
        while (c < n) c <<= 1;
        return c;
    }
    void alloc(size_t cap) {
        cap_ = cap;
        mask_ = cap - 1;
        shift_ = 64;
        for (size_t c = cap; c > 1; c >>= 1) --shift_;
        slots_.reset(new Slot[cap]);
        for (size_t i = 0; i < cap; ++i) slots_[i].used = false;
    }
    // Fibonacci hashing: multiply by 2^64/phi and keep the top bits.
    size_t home(uint64_t id) const { return static_cast<size_t>((id * 0x9E3779B97F4A7C15ull) >> shift_); }

    void grow() {
        std::unique_ptr<Slot[]> old = std::move(slots_);
        const size_t old_cap = cap_;
        alloc(cap_ * 2);
        size_ = 0;
        for (size_t i = 0; i < old_cap; ++i)
            if (old[i].used) insert(old[i].id, OrderInfo{old[i].tick, old[i].side, old[i].qty});
    }

    std::unique_ptr<Slot[]> slots_;
    size_t cap_ = 0, mask_ = 0, size_ = 0;
    int shift_ = 64;
};

class ArrayOrderBook {
public:
    static constexpr const char* kName = "C (flat array + bitmap + open-addressing ids)";

    ArrayOrderBook() {
        qty_.fill(0);
        bits_.fill(0);
    }

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick;
        if (qty <= 0 || !ticks::valid(side, price, tick)) return;
        if (!orders_.insert(id, OrderInfo{tick, side, qty})) return;
        const int i = tick - ticks::kMin;
        if (qty_[i] == 0) set_bit(i);
        qty_[i] += qty;
        if (side == Side::Buy) {
            if (i > best_bid_) best_bid_ = i;
        } else {
            if (best_ask_ < 0 || i < best_ask_) best_ask_ = i;
        }
    }

    bool cancel_order(uint64_t id) {
        OrderInfo o;
        if (!orders_.erase(id, o)) return false;
        const int i = o.tick - ticks::kMin;
        qty_[i] -= o.qty;
        if (qty_[i] == 0) {
            clear_bit(i);
            if (i == best_bid_) best_bid_ = highest_below(i);
            else if (i == best_ask_) best_ask_ = lowest_above(i);
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b{false, 0.0, 0, false, 0.0, 0};
        if (best_bid_ >= 0) {
            b.has_bid = true;
            b.bid_price = ticks::to_price(best_bid_ + ticks::kMin);
            b.bid_qty = qty_[best_bid_];
        }
        if (best_ask_ >= 0) {
            b.has_ask = true;
            b.ask_price = ticks::to_price(best_ask_ + ticks::kMin);
            b.ask_qty = qty_[best_ask_];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(depth);
        for (int i = best_bid_; i >= 0 && static_cast<int>(out.size()) < depth; i = highest_below(i))
            out.push_back({ticks::to_price(i + ticks::kMin), qty_[i]});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(depth);
        for (int i = best_ask_; i >= 0 && static_cast<int>(out.size()) < depth; i = lowest_above(i))
            out.push_back({ticks::to_price(i + ticks::kMin), qty_[i]});
        return out;
    }

private:
    static constexpr int kWords = (ticks::kCount + 63) / 64;  // 157
    // Bid indices are [0, 4999], ask indices are [5001, 10000]; index 5000
    // (price 100.00) is never set, so a scan can never cross from one side
    // into the other.
    static constexpr int kBidTop = ticks::kMaxBid - ticks::kMin;  // 4999
    static constexpr int kAskBottom = ticks::kMinAsk - ticks::kMin;  // 5001

    void set_bit(int i) { bits_[i >> 6] |= 1ull << (i & 63); }
    void clear_bit(int i) { bits_[i >> 6] &= ~(1ull << (i & 63)); }

    // Highest set bid index strictly below i, or -1.
    int highest_below(int i) const {
        if (i <= 0) return -1;
        int w = (i - 1) >> 6;
        uint64_t word = bits_[w] & (~0ull >> (63 - ((i - 1) & 63)));
        while (true) {
            if (word) {
                const int r = (w << 6) + 63 - __builtin_clzll(word);
                return r <= kBidTop ? r : -1;
            }
            if (--w < 0) return -1;
            word = bits_[w];
        }
    }

    // Lowest set ask index strictly above i, or -1.
    int lowest_above(int i) const {
        const int s = i + 1;
        if (s >= ticks::kCount) return -1;
        int w = s >> 6;
        uint64_t word = bits_[w] & (~0ull << (s & 63));
        while (true) {
            if (word) {
                const int r = (w << 6) + __builtin_ctzll(word);
                return r >= kAskBottom ? r : -1;
            }
            if (++w >= kWords) return -1;
            word = bits_[w];
        }
    }

    std::array<long, ticks::kCount> qty_;
    std::array<uint64_t, kWords> bits_;
    int best_bid_ = -1;
    int best_ask_ = -1;
    OrderMap orders_;
};
