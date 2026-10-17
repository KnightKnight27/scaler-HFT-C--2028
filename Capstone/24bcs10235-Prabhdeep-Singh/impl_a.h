#pragma once
#include "orderbook.h"
#include <map>
#include <unordered_map>

class OrderBookA {
    struct OrderInfo { Side side; int tick; long qty; };

    std::map<int, long>                     bids_;
    std::map<int, long>                     asks_;
    std::unordered_map<uint64_t, OrderInfo> orders_;

public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = price_to_tick(price);
        assert(in_band(side, tick));
        orders_[id] = {side, tick, qty};
        if (side == Side::Buy) bids_[tick] += qty;
        else                   asks_[tick] += qty;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        auto& [side, tick, qty] = it->second;
        auto& book = (side == Side::Buy) ? bids_ : asks_;
        book[tick] -= qty;
        if (book[tick] == 0) book.erase(tick);
        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (!bids_.empty()) {
            auto it = bids_.rbegin();
            b.has_bid = true; b.bid_price = tick_to_price(it->first); b.bid_qty = it->second;
        }
        if (!asks_.empty()) {
            auto it = asks_.begin();
            b.has_ask = true; b.ask_price = tick_to_price(it->first); b.ask_qty = it->second;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (auto it = bids_.rbegin(); it != bids_.rend() && (int)out.size() < depth; ++it)
            out.push_back({tick_to_price(it->first), it->second});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (auto it = asks_.begin(); it != asks_.end() && (int)out.size() < depth; ++it)
            out.push_back({tick_to_price(it->first), it->second});
        return out;
    }
};
