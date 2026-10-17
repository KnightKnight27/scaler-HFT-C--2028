// Reference order book, for testing only.
//
// The most direct reading of the spec, written to be obviously right rather
// than fast: the quantity resting at each price is a plain array indexed by
// tick, and the BBO and depth are found by scanning that array from the best
// end of each band. --verify and tests/test_orderbook.cpp compare A, B and C
// against it after every operation, so A is checked too instead of serving as
// the yardstick for the other two.
#pragma once

#include <unordered_map>
#include <vector>

#include "order_book_common.hpp"

class ReferenceBook {
public:
    bool add_order(uint64_t id, Side side, double price, long qty) {
        int tick;
        return price_to_tick(price, tick) && add_order_ticks(id, side, tick, qty);
    }

    bool add_order_ticks(uint64_t id, Side side, int tick, long qty) {
        if (!valid_order(side, tick, qty) || orders_.count(id) != 0) return false;
        orders_.emplace(id, Order{tick, qty});
        qty_at_[tick] += qty;
        return true;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        qty_at_[it->second.tick] -= it->second.qty;
        orders_.erase(it);
        return true;
    }

    // Highest prices first.
    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (int t = kBidMaxTick; t >= kBidMinTick && static_cast<int>(out.size()) < depth; --t)
            if (qty_at_[t] != 0) out.push_back(Level{to_price(t), qty_at_[t]});
        return out;
    }

    // Lowest prices first.
    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (int t = kAskMinTick; t <= kAskMaxTick && static_cast<int>(out.size()) < depth; ++t)
            if (qty_at_[t] != 0) out.push_back(Level{to_price(t), qty_at_[t]});
        return out;
    }

    BBO get_bbo() const {
        BBO b{};
        const std::vector<Level> bid = get_bids(1), ask = get_asks(1);
        if (!bid.empty()) {
            b.has_bid = true;
            b.bid_price = bid[0].price;
            b.bid_qty = bid[0].qty;
        }
        if (!ask.empty()) {
            b.has_ask = true;
            b.ask_price = ask[0].price;
            b.ask_qty = ask[0].qty;
        }
        return b;
    }

    size_t live_orders() const { return orders_.size(); }

private:
    struct Order {
        int tick;
        long qty;
    };
    std::unordered_map<uint64_t, Order> orders_;
    std::vector<long> qty_at_ = std::vector<long>(kAskMaxTick + 1, 0);  // indexed by tick
};
