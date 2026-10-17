#pragma once
#include "orderbook.h"
#include <unordered_map>
#include <algorithm>
#include <climits>

class OrderBookB {
    struct OrderInfo { Side side; int tick; long qty; };

    std::unordered_map<int, long>             bids_;
    std::unordered_map<int, long>             asks_;
    std::unordered_map<uint64_t, OrderInfo>   orders_;
    int best_bid_ = -1;
    int best_ask_ = INT_MAX;

public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = price_to_tick(price);
        assert(in_band(side, tick));
        orders_[id] = {side, tick, qty};
        if (side == Side::Buy) {
            bids_[tick] += qty;
            if (tick > best_bid_) best_bid_ = tick;
        } else {
            asks_[tick] += qty;
            if (tick < best_ask_) best_ask_ = tick;
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        auto& [side, tick, qty] = it->second;
        if (side == Side::Buy) {
            bids_[tick] -= qty;
            if (bids_[tick] == 0) {
                bids_.erase(tick);
                if (tick == best_bid_) {
                    best_bid_ = -1;
                    for (auto& [t, q] : bids_) if (t > best_bid_) best_bid_ = t;
                }
            }
        } else {
            asks_[tick] -= qty;
            if (asks_[tick] == 0) {
                asks_.erase(tick);
                if (tick == best_ask_) {
                    best_ask_ = INT_MAX;
                    for (auto& [t, q] : asks_) if (t < best_ask_) best_ask_ = t;
                }
            }
        }
        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (best_bid_ >= 0) {
            b.has_bid = true;
            b.bid_price = tick_to_price(best_bid_);
            b.bid_qty   = bids_.at(best_bid_);
        }
        if (best_ask_ != INT_MAX) {
            b.has_ask = true;
            b.ask_price = tick_to_price(best_ask_);
            b.ask_qty   = asks_.at(best_ask_);
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<std::pair<int,long>> v(bids_.begin(), bids_.end());
        std::sort(v.begin(), v.end(), [](auto& a, auto& b){ return a.first > b.first; });
        std::vector<Level> out;
        for (auto& [t, q] : v) {
            if ((int)out.size() >= depth) break;
            out.push_back({tick_to_price(t), q});
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<std::pair<int,long>> v(asks_.begin(), asks_.end());
        std::sort(v.begin(), v.end());
        std::vector<Level> out;
        for (auto& [t, q] : v) {
            if ((int)out.size() >= depth) break;
            out.push_back({tick_to_price(t), q});
        }
        return out;
    }
};
