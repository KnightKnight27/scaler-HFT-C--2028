#pragma once

#include "order_book.hpp"
#include <algorithm>
#include <map>
#include <unordered_map>

using namespace std;

class OrderBookMap : public OrderBook {
private:
    struct OrderRecord {
        Side side;
        uint32_t tick;
        long qty;
    };

    map<uint32_t, long, greater<uint32_t>> bids_;
    map<uint32_t, long, less<uint32_t>> asks_;
    unordered_map<uint64_t, OrderRecord> orders_;

    template <typename M>
    static vector<Level> extract_depth(const M& levels, int depth) {
        vector<Level> result;
        result.reserve(min(static_cast<size_t>(depth), levels.size()));
        for (auto it = levels.begin(); it != levels.end() && static_cast<int>(result.size()) < depth; ++it) {
            result.push_back({tick_to_price(it->first), it->second});
        }
        return result;
    }

public:
    void add_order(uint64_t id, Side side, double price, long qty) override {
        uint32_t tick = price_to_tick(price);
        orders_[id] = OrderRecord{side, tick, qty};

        if (side == Side::Buy) {
            bids_[tick] += qty;
        } else {
            asks_[tick] += qty;
        }
    }

    bool cancel_order(uint64_t id) override {
        auto it = orders_.find(id);
        if (it == orders_.end()) {
            return false;
        }

        const auto& ord = it->second;
        auto cancel_from = [&](auto& book_side) {
            auto lit = book_side.find(ord.tick);
            if (lit != book_side.end() && (lit->second -= ord.qty) <= 0) {
                book_side.erase(lit);
            }
        };

        if (ord.side == Side::Buy) {
            cancel_from(bids_);
        } else {
            cancel_from(asks_);
        }

        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const override {
        BBO bbo{};
        if (!bids_.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = tick_to_price(bids_.begin()->first);
            bbo.bid_qty = bids_.begin()->second;
        }
        if (!asks_.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = tick_to_price(asks_.begin()->first);
            bbo.ask_qty = asks_.begin()->second;
        }
        return bbo;
    }

    vector<Level> get_bids(int depth = 10) const override {
        return extract_depth(bids_, depth);
    }

    vector<Level> get_asks(int depth = 10) const override {
        return extract_depth(asks_, depth);
    }
};
