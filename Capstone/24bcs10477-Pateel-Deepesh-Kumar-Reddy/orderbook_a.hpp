#pragma once
#include "common.hpp"
#include <map>
#include <unordered_map>
#include <vector>

class OrderBookA {
public:
    void add_order(uint64_t id, Side side, int price, long qty) {
        orders_.emplace(id, Order{side, price, qty});
        if (side == Side::Buy) bids_[price] += qty;
        else                   asks_[price] += qty;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;

        auto side = it->second.side;
        auto price = it->second.price;
        auto qty = it->second.qty;
        if (side == Side::Buy) {
            auto& level = bids_;
            auto lit = level.find(price);
            lit->second -= qty;
            if (lit->second == 0) level.erase(lit);
        } else {
            auto& level = asks_;
            auto lit = level.find(price);
            lit->second -= qty;
            if (lit->second == 0) level.erase(lit);
        }

        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{false, 0, 0, false, 0, 0};
        if (!bids_.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = bids_.begin()->first;
            bbo.bid_qty = bids_.begin()->second;
        }
        if (!asks_.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = asks_.begin()->first;
            bbo.ask_qty = asks_.begin()->second;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top_levels(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top_levels(asks_, depth); }

private:
    template <typename M>
    static std::vector<Level> top_levels(const M& m, int depth) {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(static_cast<size_t>(depth));
        for (auto it = m.begin(); it != m.end() && static_cast<int>(out.size()) < depth; ++it)
            out.push_back(Level{it->first, it->second});
        return out;
    }

    std::map<int, long, std::greater<int>> bids_;
    std::map<int, long> asks_;
    std::unordered_map<uint64_t, Order> orders_;
};
