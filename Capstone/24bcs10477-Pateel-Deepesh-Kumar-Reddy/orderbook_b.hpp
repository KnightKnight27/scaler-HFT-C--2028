#pragma once
#include "common.hpp"
#include <algorithm>
#include <map>
#include <vector>

class OrderBookB {
public:
    void add_order(uint64_t id, Side side, int price, long qty) {
        orders_.emplace(id, Order{side, price, qty});
        auto& levels = level_of(side);
        auto pos = find(levels, price, side);
        if (pos != levels.end() && pos->price == price) {
            pos->qty += qty;
        } else {
            levels.insert(pos, Level{price, qty});
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;

        auto side = it->second.side;
        auto& levels = level_of(side);
        auto pos = find(levels, it->second.price, side);
        pos->qty -= it->second.qty;
        if (pos->qty == 0) levels.erase(pos);

        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{false, 0, 0, false, 0, 0};
        if (!bids_.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = bids_.back().price;
            bbo.bid_qty = bids_.back().qty;
        }
        if (!asks_.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = asks_.back().price;
            bbo.ask_qty = asks_.back().qty;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top_levels(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top_levels(asks_, depth); }

private:
    static std::vector<Level>::iterator find(std::vector<Level>& levels, int price, Side s) {
        auto before = [s](const Level& l, int p) {
            return s == Side::Buy ? l.price < p : l.price > p;
        };
        return std::lower_bound(levels.begin(), levels.end(), price, before);
    }

    static std::vector<Level> top_levels(const std::vector<Level>& levels, int depth) {
        std::vector<Level> out;
        if (depth <= 0) return out;
        int n = static_cast<int>(levels.size());
        int take = depth < n ? depth : n;
        out.reserve(static_cast<size_t>(take));
        for (int i = 0; i < take; ++i) out.push_back(levels[static_cast<size_t>(n - 1 - i)]);
        return out;
    }

    std::vector<Level>& level_of(Side s) { return s == Side::Buy ? bids_ : asks_; }

    std::vector<Level> bids_;
    std::vector<Level> asks_;
    std::map<uint64_t, Order> orders_;
};
