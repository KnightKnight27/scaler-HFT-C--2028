#pragma once

#include "types.hpp"
#include <vector>
#include <unordered_map>
#include <algorithm>

namespace ob {

class BookVec {
public:
    void add_order(OrderId id, Side side, Tick price, Qty qty) {
        orders_[id] = Order{side, price, qty};

        if (side == Side::Buy) {
            auto it = std::lower_bound(bids_.begin(), bids_.end(), price,
                [](const Level& l, Tick p) { return l.price < p; });
            if (it != bids_.end() && it->price == price) {
                it->qty += qty;
            } else {
                bids_.insert(it, Level{price, qty});
            }
        } else {
            auto it = std::lower_bound(asks_.begin(), asks_.end(), price,
                [](const Level& l, Tick p) { return l.price > p; });
            if (it != asks_.end() && it->price == price) {
                it->qty += qty;
            } else {
                asks_.insert(it, Level{price, qty});
            }
        }
    }

    bool cancel_order(OrderId id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;

        Side s = it->second.side;
        Tick p = it->second.price;
        Qty  q = it->second.qty;

        if (s == Side::Buy) {
            auto lit = std::lower_bound(bids_.begin(), bids_.end(), p,
                [](const Level& l, Tick price) { return l.price < price; });
            lit->qty -= q;
            if (lit->qty == 0) bids_.erase(lit);
        } else {
            auto lit = std::lower_bound(asks_.begin(), asks_.end(), p,
                [](const Level& l, Tick price) { return l.price > price; });
            lit->qty -= q;
            if (lit->qty == 0) asks_.erase(lit);
        }

        orders_.erase(id);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};
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

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        for (auto it = bids_.rbegin(); it != bids_.rend() && (int)result.size() < depth; ++it) {
            result.push_back(*it);
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        for (auto it = asks_.rbegin(); it != asks_.rend() && (int)result.size() < depth; ++it) {
            result.push_back(*it);
        }
        return result;
    }

private:
    struct Order {
        Side side;
        Tick price;
        Qty  qty;
    };

    std::vector<Level>                 bids_;   // ascending, best at back
    std::vector<Level>                 asks_;   // descending, best at back
    std::unordered_map<OrderId, Order> orders_;
};

} // namespace ob