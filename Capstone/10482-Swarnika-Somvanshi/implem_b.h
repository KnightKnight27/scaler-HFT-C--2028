// implem_b.h
// Implementation B: sorted vectors for price levels, one vector for orders.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "common.h"

class OrderBookB {
public:
    OrderBookB() : bids_(true), asks_(false) {}

    // Assumes order IDs only go up (the workload gives 1, 2, 3, ...),
    // so pushing to the back keeps the orders vector sorted by ID.
    void add_order(uint64_t id, Side side, int tick, long qty) {
        orders_.push_back(Order{id, side, tick, qty, true});
        if (side == Side::Buy)
            bids_.add(tick, qty);
        else
            asks_.add(tick, qty);
    }

    // Returns false if the order doesn't exist or was already cancelled
    bool cancel_order(uint64_t id) {
        // binary search on ID
        auto it = std::lower_bound(orders_.begin(), orders_.end(), id,
                                   [](const Order& o, uint64_t x) { return o.id < x; });
        if (it == orders_.end() || it->id != id || !it->live) return false;

        it->live = false;  // keep the order in the vector, just mark it dead
        if (it->side == Side::Buy)
            bids_.remove(it->tick, it->qty);
        else
            asks_.remove(it->tick, it->qty);
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
        if (!bids_.levels.empty()) {
            b.has_bid  = true;
            b.bid_tick = bids_.levels.back().tick;
            b.bid_qty  = bids_.levels.back().qty;
        }
        if (!asks_.levels.empty()) {
            b.has_ask  = true;
            b.ask_tick = asks_.levels.back().tick;
            b.ask_qty  = asks_.levels.back().qty;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return bids_.top(depth); }
    std::vector<Level> get_asks(int depth = 10) const { return asks_.top(depth); }

private:
    struct Order {
        uint64_t id;
        Side     side;
        int      tick;
        long     qty;
        bool     live;
    };

    // One side of the book.
    // Bids are sorted low -> high, so the best bid (highest) is at the back.
    // Asks are sorted high -> low, so the best ask (lowest) is at the back.
    struct SideBook {
        std::vector<Level> levels;
        bool is_bid;

        explicit SideBook(bool bid) : is_bid(bid) {}

        // true if a level with tick 'a' comes before tick 'b' in the vector
        bool before(int a, int b) const { return is_bid ? a < b : a > b; }

        // first level that is NOT before 'tick' (binary search)
        std::vector<Level>::iterator find_pos(int tick) {
            return std::lower_bound(levels.begin(), levels.end(), tick,
                                    [this](const Level& l, int t) { return before(l.tick, t); });
        }

        void add(int tick, long qty) {
            auto it = find_pos(tick);
            if (it != levels.end() && it->tick == tick)
                it->qty += qty;                       // level already exists
            else
                levels.insert(it, Level{tick, qty});  // new level, shifts elements
        }

        void remove(int tick, long qty) {
            auto it = find_pos(tick);                 // level must exist
            it->qty -= qty;
            if (it->qty == 0) levels.erase(it);       // shifts elements
        }

        // walk backwards from the best price
        std::vector<Level> top(int depth) const {
            std::vector<Level> out;
            out.reserve(depth);
            for (auto it = levels.rbegin(); it != levels.rend() && (int)out.size() < depth; ++it)
                out.push_back(*it);
            return out;
        }
    };

    SideBook bids_;
    SideBook asks_;
    std::vector<Order> orders_;
};
