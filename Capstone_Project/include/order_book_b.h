#pragma once
// =============================================================================
// Implementation B: Sorted std::vector-based Order Book
// =============================================================================
// Design:
//   - Price levels are stored in two sorted std::vector<Level> (one per side).
//     Bid levels are sorted descending (best bid = front).
//     Ask levels are sorted ascending  (best ask = front).
//     Binary search locates a level in O(log N).
//     Insertion/deletion shifts elements → O(N) worst-case, but N ≤ 10 000
//     and vectors are cache-friendly so constant factors are small.
//
//   - Per-order metadata stored in std::unordered_map<uint64_t, Order>.
//
// Complexity:
//   add_order    : O(log N) search + O(N) shift (amortised)
//   cancel_order : O(1) unordered_map lookup + O(log N) search + O(N) shift
//   get_bbo      : O(1) — always front of each vector
//   get_bids     : O(D) — D = depth; just slice front
//   get_asks     : O(D) — D = depth; just slice front
//
// N = number of active price levels (≤ 10 000)
// =============================================================================

#include "common.h"
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <cstdint>

class OrderBookB {
public:
    // --- Mutators -----------------------------------------------------------

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = price_to_tick(price);
        orders_[id] = Order{id, side, tick, qty};

        if (side == Side::Buy) {
            insert_or_add(bids_, tick, qty, /*desc=*/true);
        } else {
            insert_or_add(asks_, tick, qty, /*desc=*/false);
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;

        const Order& o = it->second;
        if (o.side == Side::Buy) {
            remove_qty(bids_, o.tick, o.qty, /*desc=*/true);
        } else {
            remove_qty(asks_, o.tick, o.qty, /*desc=*/false);
        }
        orders_.erase(it);
        return true;
    }

    // --- Queries ------------------------------------------------------------

    BBO get_bbo() const {
        BBO bbo;
        if (!bids_.empty()) {
            bbo.has_bid   = true;
            bbo.bid_price = tick_to_price(bids_.front().tick);
            bbo.bid_qty   = bids_.front().qty;
        }
        if (!asks_.empty()) {
            bbo.has_ask   = true;
            bbo.ask_price = tick_to_price(asks_.front().tick);
            bbo.ask_qty   = asks_.front().qty;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        return to_levels(bids_, depth);
    }

    std::vector<Level> get_asks(int depth = 10) const {
        return to_levels(asks_, depth);
    }

private:
    // Internal compact price level (using integer ticks)
    struct PriceLevel {
        int  tick;
        long qty;
    };

    // ---- helpers -----------------------------------------------------------

    // Binary-search the sorted vector for `tick`.
    // If descending=true the vector is sorted high→low.
    // Returns the iterator where the tick would live (or does live).
    static std::vector<PriceLevel>::iterator
    find_level(std::vector<PriceLevel>& v, int tick, bool desc) {
        if (desc) {
            // descending: use lower_bound with reverse comparator
            return std::lower_bound(v.begin(), v.end(), tick,
                [](const PriceLevel& lv, int t) { return lv.tick > t; });
        } else {
            return std::lower_bound(v.begin(), v.end(), tick,
                [](const PriceLevel& lv, int t) { return lv.tick < t; });
        }
    }

    static std::vector<PriceLevel>::const_iterator
    find_level(const std::vector<PriceLevel>& v, int tick, bool desc) {
        if (desc) {
            return std::lower_bound(v.begin(), v.end(), tick,
                [](const PriceLevel& lv, int t) { return lv.tick > t; });
        } else {
            return std::lower_bound(v.begin(), v.end(), tick,
                [](const PriceLevel& lv, int t) { return lv.tick < t; });
        }
    }

    static void insert_or_add(std::vector<PriceLevel>& v, int tick, long qty, bool desc) {
        auto it = find_level(v, tick, desc);
        if (it != v.end() && it->tick == tick) {
            it->qty += qty;
        } else {
            v.insert(it, PriceLevel{tick, qty});
        }
    }

    static void remove_qty(std::vector<PriceLevel>& v, int tick, long qty, bool desc) {
        auto it = find_level(v, tick, desc);
        if (it == v.end() || it->tick != tick) return; // defensive
        it->qty -= qty;
        if (it->qty == 0) v.erase(it);
    }

    static std::vector<Level> to_levels(const std::vector<PriceLevel>& v, int depth) {
        std::vector<Level> result;
        int n = static_cast<int>(v.size());
        int lim = (depth < n) ? depth : n;
        result.reserve(static_cast<size_t>(lim));
        for (int i = 0; i < lim; ++i) {
            result.push_back({tick_to_price(v[i].tick), v[i].qty});
        }
        return result;
    }

    // ---- data members ------------------------------------------------------

    std::vector<PriceLevel> bids_;  // sorted descending by tick (best bid = front)
    std::vector<PriceLevel> asks_;  // sorted ascending  by tick (best ask = front)

    std::unordered_map<uint64_t, Order> orders_;
};
