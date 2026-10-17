#pragma once

#include <cmath>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "order_book_common.hpp"

class OrderBookB {
    using Tick = int;

    struct Order {
        std::uint64_t id;
        Side side;
        Tick tick;
        long qty;
        std::uint64_t arrival_sequence;
    };

    struct BidPriority {
        bool operator()(const Order& a, const Order& b) const {
            if (a.tick != b.tick) return a.tick > b.tick;
            if (a.arrival_sequence != b.arrival_sequence) return a.arrival_sequence < b.arrival_sequence;
            return a.id < b.id;
        }
    };

    struct AskPriority {
        bool operator()(const Order& a, const Order& b) const {
            if (a.tick != b.tick) return a.tick < b.tick;
            if (a.arrival_sequence != b.arrival_sequence) return a.arrival_sequence < b.arrival_sequence;
            return a.id < b.id;
        }
    };

    using BidSet = std::multiset<Order, BidPriority>;
    using AskSet = std::multiset<Order, AskPriority>;

    BidSet bids_;
    AskSet asks_;
    std::unordered_map<std::uint64_t, BidSet::iterator> bid_lookup_;
    std::unordered_map<std::uint64_t, AskSet::iterator> ask_lookup_;
    std::unordered_set<std::uint64_t> seen_ids_;
    std::uint64_t next_sequence_ = 0;

    static bool to_tick(Side side, double price, Tick& tick) {
        if (!std::isfinite(price)) return false;
        if (side == Side::Buy) {
            if (price < 50.00 || price > 99.99) return false;
        } else {
            if (price < 100.01 || price > 150.00) return false;
        }
        double scaled = price * 100.0;
        long rounded = std::lround(scaled);
        if (std::abs(scaled - rounded) > 1e-6) return false;
        tick = static_cast<Tick>(rounded);
        return true;
    }

public:
    void add_order(std::uint64_t id, Side side, double price, long qty) {
        Tick tick;
        if (qty <= 0 || !to_tick(side, price, tick)) return;
        if (!seen_ids_.insert(id).second) return;
        Order o{id, side, tick, qty, next_sequence_++};
        if (side == Side::Buy) bid_lookup_[id] = bids_.insert(o);
        else                   ask_lookup_[id] = asks_.insert(o);
    }

    bool cancel_order(std::uint64_t id) {
        auto b = bid_lookup_.find(id);
        if (b != bid_lookup_.end()) {
            bids_.erase(b->second);
            bid_lookup_.erase(b);
            return true;
        }
        auto a = ask_lookup_.find(id);
        if (a != ask_lookup_.end()) {
            asks_.erase(a->second);
            ask_lookup_.erase(a);
            return true;
        }
        return false;
    }

    BBO get_bbo() const {
        BBO bbo;
        std::vector<Level> bids = get_bids(1);
        std::vector<Level> asks = get_asks(1);
        if (!bids.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = bids[0].price;
            bbo.bid_qty = bids[0].qty;
        }
        if (!asks.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = asks[0].price;
            bbo.ask_qty = asks[0].qty;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        if (depth <= 0) return {};
        std::vector<Level> v;
        for (const Order& o : bids_) {
            double price = o.tick / 100.0;
            if (!v.empty() && v.back().price == price) {
                v.back().qty += o.qty;
            } else {
                if ((int)v.size() == depth) break;
                v.push_back({price, o.qty});
            }
        }
        return v;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        if (depth <= 0) return {};
        std::vector<Level> v;
        for (const Order& o : asks_) {
            double price = o.tick / 100.0;
            if (!v.empty() && v.back().price == price) {
                v.back().qty += o.qty;
            } else {
                if ((int)v.size() == depth) break;
                v.push_back({price, o.qty});
            }
        }
        return v;
    }
};
