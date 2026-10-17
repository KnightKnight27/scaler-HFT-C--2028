#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "order_book_common.hpp"

class OrderBookA {
    using Tick = int;

    struct Order {
        std::uint64_t id;
        Side side;
        Tick tick;
        long qty;
    };

    struct BidCmp { bool operator()(const Order& a, const Order& b) const { return a.tick < b.tick; } };
    struct AskCmp { bool operator()(const Order& a, const Order& b) const { return a.tick > b.tick; } };

    std::unordered_map<std::uint64_t, Order> orders_;
    std::unordered_set<std::uint64_t> seen_ids_;
    std::unordered_map<Tick, long> bid_levels_, ask_levels_;

    mutable std::priority_queue<Order, std::vector<Order>, BidCmp> bid_queue_;
    mutable std::priority_queue<Order, std::vector<Order>, AskCmp> ask_queue_;

    static Tick to_tick(Side side, double price) {
        if (!std::isfinite(price)) return 0;
        if (side == Side::Buy && (price < 50.00 || price > 99.99)) return 0;
        if (side == Side::Sell && (price < 100.01 || price > 150.00)) return 0;
        Tick tick = static_cast<Tick>(std::lround(price * 100.0));
        if (std::abs(price * 100.0 - tick) > 1e-6) return 0;
        return tick;
    }

    static std::vector<Level> levels(const std::unordered_map<Tick, long>& m, int depth, bool descending) {
        std::vector<Level> v;
        if (depth <= 0) return v;
        v.reserve(m.size());
        for (auto [tick, qty] : m) v.push_back({tick / 100.0, qty});
        if (descending) std::sort(v.begin(), v.end(), [](const Level& a, const Level& b) { return a.price > b.price; });
        else            std::sort(v.begin(), v.end(), [](const Level& a, const Level& b) { return a.price < b.price; });
        if (v.size() > static_cast<std::size_t>(depth)) v.resize(depth);
        return v;
    }

public:
    void add_order(std::uint64_t id, Side side, double price, long qty) {
        Tick tick = to_tick(side, price);
        if (tick == 0 || qty <= 0) return;
        if (!seen_ids_.insert(id).second) return;
        Order o{id, side, tick, qty};
        orders_.emplace(id, o);
        if (side == Side::Buy) { bid_levels_[tick] += qty; bid_queue_.push(o); }
        else                   { ask_levels_[tick] += qty; ask_queue_.push(o); }
    }

    bool cancel_order(std::uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        const Order& o = it->second;
        auto& lv = (o.side == Side::Buy) ? bid_levels_ : ask_levels_;
        auto l = lv.find(o.tick);
        if ((l->second -= o.qty) <= 0) lv.erase(l);
        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo;
        while (!bid_queue_.empty() && !orders_.count(bid_queue_.top().id)) bid_queue_.pop();
        if (!bid_queue_.empty()) {
            Tick t = bid_queue_.top().tick;
            bbo.has_bid = true;
            bbo.bid_price = t / 100.0;
            bbo.bid_qty = bid_levels_.at(t);
        }
        while (!ask_queue_.empty() && !orders_.count(ask_queue_.top().id)) ask_queue_.pop();
        if (!ask_queue_.empty()) {
            Tick t = ask_queue_.top().tick;
            bbo.has_ask = true;
            bbo.ask_price = t / 100.0;
            bbo.ask_qty = ask_levels_.at(t);
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const { return levels(bid_levels_, depth, true); }
    std::vector<Level> get_asks(int depth = 10) const { return levels(ask_levels_, depth, false); }
};
