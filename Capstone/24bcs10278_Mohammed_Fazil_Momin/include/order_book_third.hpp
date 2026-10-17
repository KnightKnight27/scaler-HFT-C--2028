#pragma once

#include <cmath>
#include <cstdint>
#include <iterator>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "order_book_common.hpp"

class OrderBookC {
    static constexpr int MIN_BID = 5000;
    static constexpr int MAX_BID = 9999;
    static constexpr int MIN_ASK = 10001;
    static constexpr int MAX_ASK = 15000;
    static constexpr int LEVELS = 5000;

    struct Order {
        std::uint64_t id;
        Side side;
        int tick;
        long qty;
        std::uint64_t arrival_sequence;
    };

    struct PriceLevel {
        long total_qty = 0;
        std::list<Order> orders;
    };

    std::vector<PriceLevel> bids_ = std::vector<PriceLevel>(LEVELS);
    std::vector<PriceLevel> asks_ = std::vector<PriceLevel>(LEVELS);
    int best_bid_ = MIN_BID - 1;
    int best_ask_ = MAX_ASK + 1;
    std::unordered_map<std::uint64_t, std::list<Order>::iterator> lookup_;
    std::unordered_set<std::uint64_t> seen_ids_;
    std::uint64_t next_sequence_ = 0;

    PriceLevel& level(Side side, int tick) {
        return side == Side::Buy ? bids_[tick - MIN_BID] : asks_[tick - MIN_ASK];
    }
    const PriceLevel& level(Side side, int tick) const {
        return side == Side::Buy ? bids_[tick - MIN_BID] : asks_[tick - MIN_ASK];
    }

    static int to_tick(Side side, double price) {
        if (!std::isfinite(price)) return 0;
        if (side == Side::Buy && (price < 50.00 || price > 99.99)) return 0;
        if (side == Side::Sell && (price < 100.01 || price > 150.00)) return 0;
        int tick = static_cast<int>(std::lround(price * 100.0));
        if (std::abs(price * 100.0 - tick) > 1e-6) return 0;
        return tick;
    }

public:
    void add_order(std::uint64_t id, Side side, double price, long qty) {
        int tick = to_tick(side, price);
        if (tick == 0 || qty <= 0) return;
        if (!seen_ids_.insert(id).second) return;
        PriceLevel& lvl = level(side, tick);
        lvl.orders.push_back(Order{id, side, tick, qty, next_sequence_++});
        lvl.total_qty += qty;
        lookup_[id] = std::prev(lvl.orders.end());
        if (side == Side::Buy && tick > best_bid_) best_bid_ = tick;
        if (side == Side::Sell && tick < best_ask_) best_ask_ = tick;
    }

    bool cancel_order(std::uint64_t id) {
        auto it = lookup_.find(id);
        if (it == lookup_.end()) return false;
        const Order& o = *it->second;
        PriceLevel& lvl = level(o.side, o.tick);
        lvl.total_qty -= o.qty;
        lvl.orders.erase(it->second);
        lookup_.erase(it);
        while (best_bid_ >= MIN_BID && level(Side::Buy, best_bid_).total_qty == 0) --best_bid_;
        while (best_ask_ <= MAX_ASK && level(Side::Sell, best_ask_).total_qty == 0) ++best_ask_;
        return true;
    }

    BBO get_bbo() const {
        BBO bbo;
        if (best_bid_ >= MIN_BID) {
            bbo.has_bid = true;
            bbo.bid_price = best_bid_ / 100.0;
            bbo.bid_qty = level(Side::Buy, best_bid_).total_qty;
        }
        if (best_ask_ <= MAX_ASK) {
            bbo.has_ask = true;
            bbo.ask_price = best_ask_ / 100.0;
            bbo.ask_qty = level(Side::Sell, best_ask_).total_qty;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        if (depth <= 0) return {};
        std::vector<Level> v;
        for (int t = best_bid_; t >= MIN_BID && (int)v.size() < depth; --t) {
            long qty = level(Side::Buy, t).total_qty;
            if (qty > 0) v.push_back({t / 100.0, qty});
        }
        return v;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        if (depth <= 0) return {};
        std::vector<Level> v;
        for (int t = best_ask_; t <= MAX_ASK && (int)v.size() < depth; ++t) {
            long qty = level(Side::Sell, t).total_qty;
            if (qty > 0) v.push_back({t / 100.0, qty});
        }
        return v;
    }
};
