// Implementation C (own design): price-indexed array + order vector indexed by id
#pragma once
#include <cstdint>
#include <algorithm>
#include "common.hpp"

// Levels: plain array  level_[tick - 5000] = total qty   (10,001 slots, direct indexing, no search)
// Orders: std::vector<Order> indexed directly by order id (qty == 0 means "not live")
// Best price: cached best_bid_/best_ask_ ticks. Updated on add (compare) and on cancel
//             (only when the best level becomes empty: walk to the next non-empty tick)
// Cancel = array read by id, then array update by price.
class OrderBookC {
    static const int BASE = MIN_BID;
    static const int SLOTS = MAX_ASK - MIN_BID + 1;
    static const uint64_t MAX_ID = 1u << 24;            // ids >= 16M are rejected (documented limit)

    std::vector<long> level_;
    std::vector<Order> orders_;
    int best_bid_ = -1, best_ask_ = -1;                 // tick, or -1 if that side is empty

public:
    OrderBookC() : level_(SLOTS, 0) {}

    void add_order(uint64_t id, Side side, double price, long qty) {
        int t = to_tick(price);
        if (!valid(side, t, qty) || id >= MAX_ID) return;
        if (id >= orders_.size()) orders_.resize(std::max<size_t>(id + 1, orders_.size() * 2), Order{0, 0, Side::Buy});
        if (orders_[id].qty != 0) return;               // duplicate id
        orders_[id] = Order{qty, t, side};
        level_[t - BASE] += qty;
        if (side == Side::Buy) { if (t > best_bid_) best_bid_ = t; }
        else                   { if (best_ask_ == -1 || t < best_ask_) best_ask_ = t; }
    }
    bool cancel_order(uint64_t id) {
        if (id >= orders_.size() || orders_[id].qty == 0) return false;
        Order o = orders_[id];
        orders_[id].qty = 0;
        level_[o.tick - BASE] -= o.qty;
        if (level_[o.tick - BASE] == 0) {               // level became empty
            if (o.side == Side::Buy && o.tick == best_bid_) {
                int t = o.tick - 1;
                while (t >= MIN_BID && level_[t - BASE] == 0) --t;
                best_bid_ = (t >= MIN_BID) ? t : -1;
            } else if (o.side == Side::Sell && o.tick == best_ask_) {
                int t = o.tick + 1;
                while (t <= MAX_ASK && level_[t - BASE] == 0) ++t;
                best_ask_ = (t <= MAX_ASK) ? t : -1;
            }
        }
        return true;
    }
    BBO get_bbo() const {
        BBO b = empty_bbo();
        if (best_bid_ != -1) { b.has_bid = true; b.bid_price = to_price(best_bid_); b.bid_qty = level_[best_bid_ - BASE]; }
        if (best_ask_ != -1) { b.has_ask = true; b.ask_price = to_price(best_ask_); b.ask_qty = level_[best_ask_ - BASE]; }
        return b;
    }
    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        if (best_bid_ == -1) return out;
        for (int t = best_bid_; t >= MIN_BID && (int)out.size() < depth; --t)
            if (level_[t - BASE] > 0) out.push_back({to_price(t), level_[t - BASE]});
        return out;
    }
    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        if (best_ask_ == -1) return out;
        for (int t = best_ask_; t <= MAX_ASK && (int)out.size() < depth; ++t)
            if (level_[t - BASE] > 0) out.push_back({to_price(t), level_[t - BASE]});
        return out;
    }
};