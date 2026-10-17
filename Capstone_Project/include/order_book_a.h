#pragma once
// =============================================================================
// Implementation A: std::map-based Order Book
// =============================================================================
// Design:
//   - Price levels stored in std::map<int,long> (tick→qty), one per side.
//     std::map is a red-black tree: keys are always sorted, so best bid/ask
//     are rbegin() and begin() respectively → O(log N) to find them.
//   - Per-order metadata (side + tick) stored in std::unordered_map<uint64_t, Order>.
//
// Complexity:
//   add_order    : O(log N) — map insertion / update
//   cancel_order : O(log N) — unordered_map lookup O(1) + map erase O(log N)
//   get_bbo      : O(log N) — rbegin / begin on map
//   get_bids     : O(D)     — D = depth; walk from rbegin
//   get_asks     : O(D)     — D = depth; walk from begin
//
// N = number of active price levels (≤ 10 000)
// =============================================================================

#include "common.h"
#include <map>
#include <unordered_map>
#include <vector>
#include <cstdint>

class OrderBookA {
public:
    // --- Mutators -----------------------------------------------------------

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = price_to_tick(price);
        orders_[id] = Order{id, side, tick, qty};
        if (side == Side::Buy)  bids_[tick] += qty;
        else                    asks_[tick] += qty;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;

        const Order& o = it->second;
        if (o.side == Side::Buy) {
            auto lit = bids_.find(o.tick);
            lit->second -= o.qty;
            if (lit->second == 0) bids_.erase(lit);
        } else {
            auto lit = asks_.find(o.tick);
            lit->second -= o.qty;
            if (lit->second == 0) asks_.erase(lit);
        }
        orders_.erase(it);
        return true;
    }

    // --- Queries ------------------------------------------------------------

    BBO get_bbo() const {
        BBO bbo;
        if (!bids_.empty()) {
            auto it = bids_.rbegin();   // highest key = best bid
            bbo.has_bid   = true;
            bbo.bid_price = tick_to_price(it->first);
            bbo.bid_qty   = it->second;
        }
        if (!asks_.empty()) {
            auto it = asks_.begin();    // lowest key = best ask
            bbo.has_ask   = true;
            bbo.ask_price = tick_to_price(it->first);
            bbo.ask_qty   = it->second;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        result.reserve(static_cast<size_t>(depth));
        int count = 0;
        for (auto it = bids_.rbegin(); it != bids_.rend() && count < depth; ++it, ++count) {
            result.push_back({tick_to_price(it->first), it->second});
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        result.reserve(static_cast<size_t>(depth));
        int count = 0;
        for (auto it = asks_.begin(); it != asks_.end() && count < depth; ++it, ++count) {
            result.push_back({tick_to_price(it->first), it->second});
        }
        return result;
    }

private:
    // price level storage: tick → aggregate qty
    std::map<int, long> bids_;   // sorted ascending; best bid = rbegin()
    std::map<int, long> asks_;   // sorted ascending; best ask = begin()

    // per-order metadata for O(1)-ish cancellation lookup
    std::unordered_map<uint64_t, Order> orders_;
};
