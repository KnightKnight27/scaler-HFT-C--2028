#pragma once
#include <vector>
#include <algorithm>
#include "common.h"

// B: sorted vectors for levels (best price kept at the back),
// orders in a vector sorted by id -> binary search on cancel

class OrderBookB {
    struct PriceLevel {
        int tick;
        long qty;
    };

    struct Order {
        uint64_t id;
        int tick;
        Side side;
        bool alive;
        long qty;
    };

    std::vector<PriceLevel> bids;  // ascending
    std::vector<PriceLevel> asks;  // descending
    std::vector<Order> orders;     // sorted by id, cancelled ones just marked dead

    static bool bid_less(const PriceLevel& l, int tick) { return l.tick < tick; }
    static bool ask_less(const PriceLevel& l, int tick) { return l.tick > tick; }

    std::vector<Order>::iterator find_order(uint64_t id) {
        auto it = std::lower_bound(orders.begin(), orders.end(), id,
            [](const Order& o, uint64_t v) { return o.id < v; });
        if (it != orders.end() && it->id == id)
            return it;
        return orders.end();
    }

public:
    OrderBookB() {
        orders.reserve(1 << 20);
        bids.reserve(5000);
        asks.reserve(5000);
    }

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = price_to_tick(price);
        if (qty <= 0 || !valid_tick(side, tick))
            return;

        if (orders.empty() || orders.back().id < id) {
            orders.push_back({id, tick, side, true, qty});
        } else {
            auto it = std::lower_bound(orders.begin(), orders.end(), id,
                [](const Order& o, uint64_t v) { return o.id < v; });
            if (it != orders.end() && it->id == id)
                return;  // duplicate id
            orders.insert(it, {id, tick, side, true, qty});
        }

        if (side == Side::Buy) {
            auto it = std::lower_bound(bids.begin(), bids.end(), tick, bid_less);
            if (it != bids.end() && it->tick == tick)
                it->qty += qty;
            else
                bids.insert(it, {tick, qty});
        } else {
            auto it = std::lower_bound(asks.begin(), asks.end(), tick, ask_less);
            if (it != asks.end() && it->tick == tick)
                it->qty += qty;
            else
                asks.insert(it, {tick, qty});
        }
    }

    bool cancel_order(uint64_t id) {
        auto oit = find_order(id);
        if (oit == orders.end() || !oit->alive)
            return false;
        oit->alive = false;

        if (oit->side == Side::Buy) {
            auto it = std::lower_bound(bids.begin(), bids.end(), oit->tick, bid_less);
            it->qty -= oit->qty;
            if (it->qty == 0)
                bids.erase(it);
        } else {
            auto it = std::lower_bound(asks.begin(), asks.end(), oit->tick, ask_less);
            it->qty -= oit->qty;
            if (it->qty == 0)
                asks.erase(it);
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
        if (!bids.empty()) {
            b.has_bid = true;
            b.bid_price = tick_to_price(bids.back().tick);
            b.bid_qty = bids.back().qty;
        }
        if (!asks.empty()) {
            b.has_ask = true;
            b.ask_price = tick_to_price(asks.back().tick);
            b.ask_qty = asks.back().qty;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> res;
        for (int i = (int)bids.size() - 1; i >= 0 && (int)res.size() < depth; i--)
            res.push_back({tick_to_price(bids[i].tick), bids[i].qty});
        return res;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> res;
        for (int i = (int)asks.size() - 1; i >= 0 && (int)res.size() < depth; i--)
            res.push_back({tick_to_price(asks[i].tick), asks[i].qty});
        return res;
    }
};
