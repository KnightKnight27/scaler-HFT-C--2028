#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <unordered_map>

#include "Common.h"

// Implementation A: ordered price levels via std::map (one per side).
//
// - Per-price storage: std::map<ticks, PriceLevel>, descending for bids so
//   begin() is always the best bid, ascending for asks so begin() is always
//   the best ask. Each PriceLevel keeps a running total quantity plus a
//   std::list of the individual resting order quantities.
// - Per-order storage: std::unordered_map<id, OrderLocation> recording which
//   side/price/list-node an order lives at, so cancel_order does not need to
//   scan the book (Hint 1).
//
// Complexity (L = number of distinct price levels on the relevant side):
//   add_order    O(log L)   map insert/find + O(1) list push_back
//   cancel_order O(log L)   unordered_map O(1) avg lookup + map find/erase
//   get_bbo      O(1)       map::begin() is cached (leftmost node) in libstdc++
//   get_bids/ks  O(depth)   walk the map from begin()
class OrderBookA
{
public:
    void add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

private:
    struct PriceLevel
    {
        long totalQty = 0;
        std::list<long> orders;
    };

    struct OrderLocation
    {
        Side side;
        int priceTicks;
        std::list<long>::iterator it;
    };

    std::map<int, PriceLevel, std::greater<int>> bids_;
    std::map<int, PriceLevel> asks_;
    std::unordered_map<uint64_t, OrderLocation> orders_;
};
