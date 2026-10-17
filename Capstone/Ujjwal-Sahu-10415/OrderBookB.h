#pragma once

#include <cstdint>
#include <set>
#include <unordered_map>

#include "Common.h"

// Implementation B: unordered price-level storage, with a separate sorted
// index just for tracking the best price on each side.
//
// - Per-order storage: unordered_map<id, OrderInfo> is the single source of
//   truth for every resting order (side, price, qty) -- no secondary
//   per-level order list, unlike Implementation A.
// - Per-price storage: unordered_map<ticks, qty> per side, giving O(1)
//   average add/update/remove of a level's aggregate quantity.
// - Best bid/ask: a std::set<ticks> per side tracks which price levels are
//   currently non-empty. Best bid = *bidPrices_.rbegin() (highest), best ask
//   = *askPrices_.begin() (lowest). The set is only touched when a level is
//   opened or fully emptied, not on every add/cancel.
// - Cancellation: the order map gives side/price/qty directly in O(1)
//   average, so no scan of the book is ever needed.
//
// Complexity (L = number of distinct non-empty price levels on a side):
//   add_order    O(1) avg to update an existing level, O(log L) when a new
//                price level is opened (set insert)
//   cancel_order O(1) avg to update a level, O(log L) when a level empties
//   get_bbo      O(1): set::begin()/rbegin() are cached + O(1) avg hash lookup
//   get_bids/ks  O(depth): walk the sorted set, O(1) avg hash lookup per level
class OrderBookB
{
public:
    void add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

private:
    struct OrderInfo
    {
        Side side;
        int priceTicks;
        long qty;
    };

    std::unordered_map<uint64_t, OrderInfo> orders_;

    std::unordered_map<int, long> bidQty_;
    std::unordered_map<int, long> askQty_;

    std::set<int> bidPrices_;
    std::set<int> askPrices_;
};
