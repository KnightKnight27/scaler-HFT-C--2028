// Implementation B: sorted contiguous vector of price levels per side, with the
// individual orders stored *inside* their price level (the classic
// "level-centric" book layout), plus an ordered tree (std::map) from
// id -> (side, tick).
//
//  * Orders       : std::vector<{id, qty}> inside each PriceLevel. The id tree
//                   (std::map, red-black tree keyed by order id) only remembers
//                   which level an id lives in.
//  * Price levels : std::vector<PriceLevel> kept sorted so that the *back* of
//                   the vector is the best price (bids ascending, asks
//                   descending). Inserting a brand-new level is a binary
//                   search + vector::insert, which moves every later
//                   PriceLevel one slot (element-wise moves, since each holds
//                   a vector); the best-price end is the cheap end.
//  * Best price   : levels.back()  (O(1), contiguous memory)
//  * Cancel       : tree find id -> tick (O(log N)), binary search the level,
//                   then a linear scan of that level's order list, swap-remove.
//
//  N = live orders, L = non-empty levels on the side, k = orders at one level.
//  add_order   : O(log N) tree insert + O(log L) search (+ O(L) moves if new level)
//  cancel_order: O(log N) tree find/erase + O(log L) search + O(k) scan
//                (+ O(L) moves if the level empties)
//  get_bbo     : O(1)
//  get_bids/asks(d): O(d) (walk backwards from the end of the vector)
#pragma once

#include <algorithm>
#include <map>

#include "order_book_common.hpp"

class OrderBookB {
public:
    OrderBookB() {
        bids_.reserve(kLevelsPerSide);
        asks_.reserve(kLevelsPerSide);
    }

    // Returns false (and changes nothing) for an out-of-band, off-tick or
    // non-finite price, qty <= 0, or an id that is already live.
    bool add_order(uint64_t id, Side side, double price, long qty) {
        int tick;
        return price_to_tick(price, tick) && add_order_ticks(id, side, tick, qty);
    }

    bool add_order_ticks(uint64_t id, Side side, int tick, long qty) {
        if (!valid_order(side, tick, qty)) return false;
        auto ins = where_.emplace(id, Loc{tick, side});
        if (!ins.second) return false;  // duplicate id

        if (side == Side::Buy) {
            // bids ascending: best (highest) at back
            auto it = std::lower_bound(bids_.begin(), bids_.end(), tick,
                                       [](const PriceLevel& l, int t) { return l.tick < t; });
            if (it == bids_.end() || it->tick != tick) it = bids_.insert(it, PriceLevel{tick, 0, {}});
            it->qty += qty;
            it->orders.push_back(Order{id, qty});
        } else {
            // asks descending: best (lowest) at back
            auto it = std::lower_bound(asks_.begin(), asks_.end(), tick,
                                       [](const PriceLevel& l, int t) { return l.tick > t; });
            if (it == asks_.end() || it->tick != tick) it = asks_.insert(it, PriceLevel{tick, 0, {}});
            it->qty += qty;
            it->orders.push_back(Order{id, qty});
        }
        return true;
    }

    bool cancel_order(uint64_t id) {
        auto it = where_.find(id);
        if (it == where_.end()) return false;
        const Loc loc = it->second;
        where_.erase(it);

        if (loc.side == Side::Buy) {
            auto lv = std::lower_bound(bids_.begin(), bids_.end(), loc.tick,
                                       [](const PriceLevel& l, int t) { return l.tick < t; });
            remove_order(bids_, lv, id);
        } else {
            auto lv = std::lower_bound(asks_.begin(), asks_.end(), loc.tick,
                                       [](const PriceLevel& l, int t) { return l.tick > t; });
            remove_order(asks_, lv, id);
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
        if (!bids_.empty()) {
            b.has_bid = true;
            b.bid_price = to_price(bids_.back().tick);
            b.bid_qty = bids_.back().qty;
        }
        if (!asks_.empty()) {
            b.has_ask = true;
            b.ask_price = to_price(asks_.back().tick);
            b.ask_qty = asks_.back().qty;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top(asks_, depth); }

private:
    struct Order {
        uint64_t id;
        long qty;
    };
    struct PriceLevel {
        int tick;
        long qty;                   // sum of orders[].qty
        std::vector<Order> orders;  // live orders resting at this price
    };
    struct Loc {
        int tick;
        Side side;
    };

    static void remove_order(std::vector<PriceLevel>& levels, std::vector<PriceLevel>::iterator lv,
                             uint64_t id) {
        auto& ords = lv->orders;
        for (size_t i = 0; i < ords.size(); ++i) {
            if (ords[i].id == id) {
                lv->qty -= ords[i].qty;
                ords[i] = ords.back();  // swap-remove (no FIFO needed: no matching)
                ords.pop_back();
                break;
            }
        }
        if (lv->qty == 0) levels.erase(lv);
    }

    static std::vector<Level> top(const std::vector<PriceLevel>& levels, int depth) {
        std::vector<Level> out;
        out.reserve(depth_capacity(depth));
        for (auto it = levels.rbegin(); it != levels.rend() && depth > 0; ++it, --depth)
            out.push_back(Level{to_price(it->tick), it->qty});
        return out;
    }

    std::vector<PriceLevel> bids_;  // sorted ascending, best at back
    std::vector<PriceLevel> asks_;  // sorted descending, best at back
    std::map<uint64_t, Loc> where_;  // order id -> which level it rests in
};
