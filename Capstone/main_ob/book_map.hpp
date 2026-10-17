#pragma once

#include "types.hpp"
#include <map>
#include <unordered_map>
#include <functional>
#include <vector>

namespace ob {

class BookMap {
public:
    void add_order(OrderId id, Side side, Tick price, Qty qty) {
        orders_[id] = Order{side, price, qty};

        if (side == Side::Buy) {
            bids_[price] += qty;
        } else {
            asks_[price] += qty;
        }
    }

    bool cancel_order(OrderId id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) {
            return false;
        }

        Side c_s = it->second.side;
        Tick c_p = it->second.price;
        Qty  c_q = it->second.qty;

        if (c_s == Side::Buy) {
            bids_[c_p] -= c_q;
            if (bids_[c_p] == 0) {
                bids_.erase(c_p);
            }
        } else {
            asks_[c_p] -= c_q;
            if (asks_[c_p] == 0) {
                asks_.erase(c_p);
            }
        }

        orders_.erase(id);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};

        if (bids_.empty()) {
            bbo.has_bid = false;
        } else {
            bbo.has_bid = true;
            bbo.bid_price = bids_.begin()->first;
            bbo.bid_qty = bids_.begin()->second;
        }

        if (asks_.empty()) {
            bbo.has_ask = false;
        } else {
            bbo.has_ask = true;
            bbo.ask_price = asks_.begin()->first;
            bbo.ask_qty = asks_.begin()->second;
        }

        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        for (auto it = bids_.begin(); it != bids_.end() && (int)result.size() < depth; ++it) {
            result.push_back({it->first, it->second});
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        for (auto it = asks_.begin(); it != asks_.end() && (int)result.size() < depth; ++it) {
            result.push_back({it->first, it->second});
        }
        return result;
    }

private:
    struct Order {
        Side side;
        Tick price;
        Qty  qty;
    };

    std::map<Tick, Qty, std::greater<Tick>> bids_;
    std::map<Tick, Qty>                     asks_;
    std::unordered_map<OrderId, Order>      orders_;
};

} // namespace ob