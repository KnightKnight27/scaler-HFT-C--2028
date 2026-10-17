// implem_c.h
// Implementation C: price-indexed arrays for levels, vector indexed by ID for orders.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "common.h"

class OrderBookC {
public:
    // Assumes order IDs are small and increasing (1, 2, 3, ...),
    // so the ID can be used directly as a position in orders_.
    void add_order(uint64_t id, Side side, int tick, long qty) {
        if (id >= orders_.size()) orders_.resize(id + 1);
        orders_[id] = Order{side, tick, qty, true};

        if (side == Side::Buy) {
            int idx = bid_index(tick);
            bid_qty_[idx] += qty;
            if (idx > best_bid_) best_bid_ = idx;   // new best bid
        } else {
            int idx = ask_index(tick);
            ask_qty_[idx] += qty;
            if (idx < best_ask_) best_ask_ = idx;   // new best ask
        }
    }

    // Returns false if the order doesn't exist or was already cancelled
    bool cancel_order(uint64_t id) {
        if (id >= orders_.size() || !orders_[id].live) return false;

        Order& o = orders_[id];
        o.live = false;

        if (o.side == Side::Buy) {
            int idx = bid_index(o.tick);
            bid_qty_[idx] -= o.qty;
            // if the best level just became empty, scan down to the next non-empty one
            if (idx == best_bid_ && bid_qty_[idx] == 0) {
                while (best_bid_ >= 0 && bid_qty_[best_bid_] == 0) --best_bid_;
            }
        } else {
            int idx = ask_index(o.tick);
            ask_qty_[idx] -= o.qty;
            // if the best level just became empty, scan up to the next non-empty one
            if (idx == best_ask_ && ask_qty_[idx] == 0) {
                while (best_ask_ < LEVELS_PER_SIDE && ask_qty_[best_ask_] == 0) ++best_ask_;
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
        if (best_bid_ >= 0) {
            b.has_bid  = true;
            b.bid_tick = bid_tick_from_index(best_bid_);
            b.bid_qty  = bid_qty_[best_bid_];
        }
        if (best_ask_ < LEVELS_PER_SIDE) {
            b.has_ask  = true;
            b.ask_tick = ask_tick_from_index(best_ask_);
            b.ask_qty  = ask_qty_[best_ask_];
        }
        return b;
    }

    // Walk down from the best bid, skipping empty slots
    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        out.reserve(depth);
        for (int i = best_bid_; i >= 0 && (int)out.size() < depth; --i)
            if (bid_qty_[i] != 0)
                out.push_back(Level{bid_tick_from_index(i), bid_qty_[i]});
        return out;
    }

    // Walk up from the best ask, skipping empty slots
    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        out.reserve(depth);
        for (int i = best_ask_; i < LEVELS_PER_SIDE && (int)out.size() < depth; ++i)
            if (ask_qty_[i] != 0)
                out.push_back(Level{ask_tick_from_index(i), ask_qty_[i]});
        return out;
    }

private:
    struct Order {
        Side side;
        int  tick;
        long qty;
        bool live = false;
    };

    std::array<long, LEVELS_PER_SIDE> bid_qty_{};   // total qty per bid price
    std::array<long, LEVELS_PER_SIDE> ask_qty_{};   // total qty per ask price

    int best_bid_ = -1;                 // -1 means "no bids"
    int best_ask_ = LEVELS_PER_SIDE;    // 5000 means "no asks"

    std::vector<Order> orders_;         // orders_[id]
};
