#pragma once
#include <vector>
#include "common.h"

// C: flat array indexed by tick + cached best bid/ask,
// orders stored in a vector indexed by id (assumes small dense ids)

class OrderBookC {
    struct Order {
        int tick;
        int qty;  // 0 = not live
    };

    static const int NUM_TICKS = MAX_TICK - MIN_TICK + 1;

    long level_qty[NUM_TICKS];  // both sides, bands don't overlap
    int best_bid;  // tick, or -1 if no bids
    int best_ask;  // tick, or -1 if no asks
    std::vector<Order> orders;

public:
    OrderBookC() {
        for (int i = 0; i < NUM_TICKS; i++)
            level_qty[i] = 0;
        best_bid = -1;
        best_ask = -1;
        orders.resize(1 << 20, {0, 0});
    }

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = price_to_tick(price);
        if (qty <= 0 || !valid_tick(side, tick))
            return;

        if (id >= orders.size()) {
            size_t n = orders.size();
            while (n <= id) n *= 2;
            orders.resize(n, {0, 0});
        }
        if (orders[id].qty != 0)  // duplicate
            return;

        orders[id].tick = tick;
        orders[id].qty = (int)qty;
        level_qty[tick - MIN_TICK] += qty;

        if (side == Side::Buy) {
            if (tick > best_bid) best_bid = tick;
        } else {
            if (best_ask == -1 || tick < best_ask) best_ask = tick;
        }
    }

    bool cancel_order(uint64_t id) {
        if (id >= orders.size() || orders[id].qty == 0)
            return false;

        int tick = orders[id].tick;
        level_qty[tick - MIN_TICK] -= orders[id].qty;
        orders[id].qty = 0;

        if (level_qty[tick - MIN_TICK] == 0) {
            // best level emptied -> walk to next one
            if (tick == best_bid) {
                int t = tick - 1;
                while (t >= MIN_TICK && level_qty[t - MIN_TICK] == 0) t--;
                best_bid = (t >= MIN_TICK) ? t : -1;
            } else if (tick == best_ask) {
                int t = tick + 1;
                while (t <= MAX_TICK && level_qty[t - MIN_TICK] == 0) t++;
                best_ask = (t <= MAX_TICK) ? t : -1;
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
        if (best_bid != -1) {
            b.has_bid = true;
            b.bid_price = tick_to_price(best_bid);
            b.bid_qty = level_qty[best_bid - MIN_TICK];
        }
        if (best_ask != -1) {
            b.has_ask = true;
            b.ask_price = tick_to_price(best_ask);
            b.ask_qty = level_qty[best_ask - MIN_TICK];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> res;
        if (best_bid == -1) return res;
        for (int t = best_bid; t >= MIN_TICK && (int)res.size() < depth; t--) {
            if (level_qty[t - MIN_TICK] != 0)
                res.push_back({tick_to_price(t), level_qty[t - MIN_TICK]});
        }
        return res;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> res;
        if (best_ask == -1) return res;
        for (int t = best_ask; t <= MAX_TICK && (int)res.size() < depth; t++) {
            if (level_qty[t - MIN_TICK] != 0)
                res.push_back({tick_to_price(t), level_qty[t - MIN_TICK]});
        }
        return res;
    }
};
