// impl_c_array.h — Implementation C (my own design): dense price arrays + bitmap.
//
// Everything is contiguous and index-addressed; there is no per-level allocation
// and no pointer chasing.
//
//   Price levels : two flat std::vector<long> of 5000 slots (one per possible
//                  tick), indexed by (tick - band_min). Value = total qty.
//   Order table  : std::vector<Order> indexed DIRECTLY by order id. Cancellation
//                  is therefore an O(1) array index (no hashing) -- this is how C
//                  differs from A/B in "locating orders for cancellation".
//                  Assumes dense, monotonic ids (the benchmark assigns 1,2,3,...);
//                  documented in the README.
//   Best price   : a 1-bit-per-level BITMAP (uint64_t[79] per side) marks the
//                  non-empty levels. Best bid = highest set bit, best ask = lowest
//                  set bit, found with __builtin_clzll / __builtin_ctzll over at
//                  most 79 words -> effectively O(1), branch-light, cache-friendly.
//   get_bids/asks: walk the bitmap from the best level outward -> O(depth).
//
// add_order / cancel_order / get_bbo are all ~O(1); memory is one bid array + one
// ask array + one order vector, all sequential.

#ifndef CAPSTONE_IMPL_C_ARRAY_H
#define CAPSTONE_IMPL_C_ARRAY_H

#include <cstdint>
#include <vector>

#include "common.h"

class OrderBookC {
public:
    OrderBookC()
        : bid_qty_(ticks::kLevelsPerSide, 0),
          ask_qty_(ticks::kLevelsPerSide, 0) {
        for (int i = 0; i < kWords; ++i) {
            bid_bits_[i] = 0;
            ask_bits_[i] = 0;
        }
        orders_.reserve(1 << 20);
    }

    void add_order(uint64_t id, Side side, double price, long qty) {
        const int tick = ticks::to_tick(price);
        if (id >= orders_.size()) orders_.resize(id + 1);
        orders_[id] = Order{tick, qty, side, true};

        if (side == Side::Buy) {
            const int off = tick - ticks::kBidMin;
            if (bid_qty_[off] == 0) set_bit(bid_bits_, off);
            bid_qty_[off] += qty;
        } else {
            const int off = tick - ticks::kAskMin;
            if (ask_qty_[off] == 0) set_bit(ask_bits_, off);
            ask_qty_[off] += qty;
        }
    }

    bool cancel_order(uint64_t id) {
        if (id >= orders_.size()) return false;
        Order& o = orders_[id];
        if (!o.live) return false;  // unknown slot or already cancelled
        o.live = false;

        if (o.side == Side::Buy) {
            const int off = o.tick - ticks::kBidMin;
            bid_qty_[off] -= o.qty;
            if (bid_qty_[off] <= 0) clear_bit(bid_bits_, off);
        } else {
            const int off = o.tick - ticks::kAskMin;
            ask_qty_[off] -= o.qty;
            if (ask_qty_[off] <= 0) clear_bit(ask_bits_, off);
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        const int bid_off = highest_set(bid_bits_);
        if (bid_off >= 0) {
            b.has_bid = true;
            b.bid_price = ticks::to_price(bid_off + ticks::kBidMin);
            b.bid_qty = bid_qty_[bid_off];
        }
        const int ask_off = lowest_set(ask_bits_);
        if (ask_off >= 0) {
            b.has_ask = true;
            b.ask_price = ticks::to_price(ask_off + ticks::kAskMin);
            b.ask_qty = ask_qty_[ask_off];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        out.reserve(depth);
        int count = 0;
        for (int w = kWords - 1; w >= 0 && count < depth; --w) {
            uint64_t word = bid_bits_[w];
            while (word && count < depth) {
                const int b = 63 - __builtin_clzll(word);  // highest set bit
                const int off = w * 64 + b;
                out.push_back({ticks::to_price(off + ticks::kBidMin), bid_qty_[off]});
                word &= ~(1ULL << b);
                ++count;
            }
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        out.reserve(depth);
        int count = 0;
        for (int w = 0; w < kWords && count < depth; ++w) {
            uint64_t word = ask_bits_[w];
            while (word && count < depth) {
                const int b = __builtin_ctzll(word);  // lowest set bit
                const int off = w * 64 + b;
                out.push_back({ticks::to_price(off + ticks::kAskMin), ask_qty_[off]});
                word &= word - 1;  // clear lowest set bit
                ++count;
            }
        }
        return out;
    }

private:
    struct Order {
        int tick = 0;
        long qty = 0;
        Side side = Side::Buy;
        bool live = false;
    };

    static constexpr int kWords = (ticks::kLevelsPerSide + 63) / 64;  // 79

    static void set_bit(uint64_t* bits, int off) { bits[off >> 6] |= (1ULL << (off & 63)); }
    static void clear_bit(uint64_t* bits, int off) { bits[off >> 6] &= ~(1ULL << (off & 63)); }

    static int highest_set(const uint64_t* bits) {
        for (int w = kWords - 1; w >= 0; --w)
            if (bits[w]) return w * 64 + (63 - __builtin_clzll(bits[w]));
        return -1;
    }

    static int lowest_set(const uint64_t* bits) {
        for (int w = 0; w < kWords; ++w)
            if (bits[w]) return w * 64 + __builtin_ctzll(bits[w]);
        return -1;
    }

    std::vector<long> bid_qty_;
    std::vector<long> ask_qty_;
    uint64_t bid_bits_[kWords];
    uint64_t ask_bits_[kWords];
    std::vector<Order> orders_;
};

#endif  // CAPSTONE_IMPL_C_ARRAY_H
