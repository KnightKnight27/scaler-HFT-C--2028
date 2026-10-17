#pragma once
#include <vector>
#include "common.h"

// Implementation C: my own design, flat arrays
//
// there are only 10,001 possible prices, so instead of searching for a price
// I use the price itself as the index into an array
// index = tick - 5000, so 50.00 is index 0, 100.00 is index 5000, 150.00 is index 10000
//
// bids and asks live in the SAME array, bids on the left of the mid and asks on the right
// (this is the idea sir showed from the paper, both best prices sit close to the middle)
//
// orders: one big vector of Order indexed directly by order id (like the memory pool homework)
// levels: one fixed array of qty, one slot per tick
// best price: two cached indexes that we update on add and cancel
class ArrayOrderBook {
public:
    ArrayOrderBook() {
        for (int32_t i = 0; i < kNumSlots; i++) {
            levels_[i] = 0;
        }
        best_.bid = kNoBid;
        best_.ask = kNoAsk;
        // one big allocation at the start instead of many small ones later
        // qty 0 means the slot is free or the order was cancelled
        pool_.resize(kStartPoolSize, Order{0, 0, Side::Buy});
    }

    void add_order(uint64_t id, Side side, int32_t tick, long qty) {
        if (!valid_order(side, tick, qty)) {
            return;
        }
        if (id >= pool_.size()) {
            grow_pool(id);
        }
        if (pool_[id].qty != 0) {
            return;  // id already live
        }
        pool_[id] = Order{qty, tick, side};

        int32_t i = tick - kMinBidTick;
        levels_[i] += qty;

        // a new order can only make the best price better, never worse
        if (side == Side::Buy) {
            if (i > best_.bid) {
                best_.bid = i;
            }
        } else {
            if (i < best_.ask) {
                best_.ask = i;
            }
        }
    }

    bool cancel_order(uint64_t id) {
        if (id >= pool_.size()) {
            return false;  // never seen this id
        }
        Order& o = pool_[id];
        if (o.qty == 0) {
            return false;  // free slot or already cancelled
        }
        int32_t i = o.tick - kMinBidTick;
        levels_[i] -= o.qty;
        o.qty = 0;

        // most of the time the level still has other orders, so nothing else to do
        if (levels_[i] != 0) [[likely]] {
            return true;
        }

        // the level is empty now. only if it was the best level we need to find the next one
        // we walk away from the mid until we find a slot with qty
        // this is the only loop in the hot path and it is rare
        if (o.side == Side::Buy) {
            if (i == best_.bid) [[unlikely]] {
                while (best_.bid >= 0 && levels_[best_.bid] == 0) {
                    best_.bid--;
                }
            }
        } else {
            if (i == best_.ask) [[unlikely]] {
                while (best_.ask < kNumSlots && levels_[best_.ask] == 0) {
                    best_.ask++;
                }
            }
        }
        return true;
    }

    // no search at all, just read two cached indexes and two array slots
    BBO get_bbo() const {
        BBO b = {false, 0, 0, false, 0, 0};
        if (best_.bid >= 0) {
            b.has_bid = true;
            b.bid_tick = best_.bid + kMinBidTick;
            b.bid_qty = levels_[best_.bid];
        }
        if (best_.ask < kNumSlots) {
            b.has_ask = true;
            b.ask_tick = best_.ask + kMinBidTick;
            b.ask_qty = levels_[best_.ask];
        }
        return b;
    }

    // walk left from the best bid and skip empty slots
    // memory is contiguous so this walk is very cache friendly
    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (int32_t i = best_.bid; i >= 0 && (int)out.size() < depth; i--) {
            if (levels_[i] != 0) {
                out.push_back(Level{i + kMinBidTick, levels_[i]});
            }
        }
        return out;
    }

    // walk right from the best ask
    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (int32_t i = best_.ask; i < kNumSlots && (int)out.size() < depth; i++) {
            if (levels_[i] != 0) {
                out.push_back(Level{i + kMinBidTick, levels_[i]});
            }
        }
        return out;
    }

private:
    const int32_t kNoBid = -1;         // one slot left of the array
    const int32_t kNoAsk = kNumSlots;  // one slot right of the array
    static const size_t kStartPoolSize = 1 << 20;  // about one million orders, 16 MB

    // order ids are not reused, so if an id is bigger than the pool we double it
    // this assumes ids are small numbers that go up one by one, like an exchange gives
    void grow_pool(uint64_t id) {
        size_t new_size = pool_.size() * 2;
        if (new_size <= id) {
            new_size = id + 1;
        }
        pool_.resize(new_size, Order{0, 0, Side::Buy});
    }

    // get_bbo reads both of these every time, so I keep them together
    // alignas(64) puts them at the start of their own cache line like we did in the spsc queue
    struct alignas(64) BestIndex {
        int32_t bid;
        int32_t ask;
    };

    BestIndex best_;
    long levels_[kNumSlots];   // 10,001 * 8 bytes = about 80 KB, one flat block
    std::vector<Order> pool_;  // order id is the index
};
