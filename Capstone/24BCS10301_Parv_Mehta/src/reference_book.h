#pragma once
// Slow but obviously correct book, used only as the oracle in the tests and
// in --verify. It stores raw orders and recomputes levels on every query.

#include <unordered_map>
#include <vector>

#include "order_book.h"

class ReferenceBook {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        Tick tick = to_tick(price);
        if (qty <= 0 || !tick_in_band(side, tick)) return;
        orders_.emplace(id, Order{side, tick, qty});
    }

    bool cancel_order(uint64_t id) { return orders_.erase(id) > 0; }

    BBO get_bbo() const {
        BBO b{};
        std::vector<Level> bids = get_bids(1);
        std::vector<Level> asks = get_asks(1);
        if (!bids.empty()) { b.has_bid = true; b.bid_price = bids[0].price; b.bid_qty = bids[0].qty; }
        if (!asks.empty()) { b.has_ask = true; b.ask_price = asks[0].price; b.ask_qty = asks[0].qty; }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<long> sum(kLevelsPerSide, 0);
        for (const auto& kv : orders_)
            if (kv.second.side == Side::Buy) sum[kv.second.tick - kMinBidTick] += kv.second.qty;
        std::vector<Level> out;
        for (int i = kLevelsPerSide - 1; i >= 0 && (int)out.size() < depth; i--)
            if (sum[i] > 0) out.push_back(Level{to_price(kMinBidTick + i), sum[i]});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<long> sum(kLevelsPerSide, 0);
        for (const auto& kv : orders_)
            if (kv.second.side == Side::Sell) sum[kv.second.tick - kMinAskTick] += kv.second.qty;
        std::vector<Level> out;
        for (int i = 0; i < kLevelsPerSide && (int)out.size() < depth; i++)
            if (sum[i] > 0) out.push_back(Level{to_price(kMinAskTick + i), sum[i]});
        return out;
    }

    size_t order_count() const { return orders_.size(); }

private:
    struct Order {
        Side side;
        Tick tick;
        long qty;
    };
    std::unordered_map<uint64_t, Order> orders_;
};

inline bool same_bbo(const BBO& x, const BBO& y) {
    if (x.has_bid != y.has_bid || x.has_ask != y.has_ask) return false;
    if (x.has_bid && (to_tick(x.bid_price) != to_tick(y.bid_price) || x.bid_qty != y.bid_qty)) return false;
    if (x.has_ask && (to_tick(x.ask_price) != to_tick(y.ask_price) || x.ask_qty != y.ask_qty)) return false;
    return true;
}

inline bool same_levels(const std::vector<Level>& x, const std::vector<Level>& y) {
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); i++)
        if (to_tick(x[i].price) != to_tick(y[i].price) || x[i].qty != y[i].qty) return false;
    return true;
}
