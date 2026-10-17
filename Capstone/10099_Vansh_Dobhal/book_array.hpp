#pragma once

// Implementation C (my own design)
// price levels: one flat array of qty indexed directly by tick (10000 levels fit easily)
// best bid / best ask: cached as indexes, only rescanned when the best level becomes empty
// orders: std::vector indexed by order id (ids are unique and given out in increasing order)

#include "types.hpp"

class ArrayBook {
public:
    ArrayBook() {
        for (int i = 0; i < NUM_TICKS; i++)
            qty_at[i] = 0;
    }

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = to_tick(price);
        if (qty <= 0 || !valid_price(side, tick))
            return;
        if (id >= orders.size())
            orders.resize(id * 2 + 16);
        if (orders[id].qty != 0)
            return; // id already live

        orders[id] = {tick, qty};
        qty_at[tick] += qty;

        if (side == Side::Buy) {
            if (tick > best_bid)
                best_bid = tick;
        } else {
            if (tick < best_ask)
                best_ask = tick;
        }
    }

    bool cancel_order(uint64_t id) {
        if (id >= orders.size() || orders[id].qty == 0)
            return false;

        int tick = orders[id].tick;
        qty_at[tick] -= orders[id].qty;
        orders[id].qty = 0;

        // if the best level just became empty, walk to the next non empty level
        if (qty_at[tick] == 0) {
            if (tick == best_bid) {
                while (best_bid >= MIN_BID_TICK && qty_at[best_bid] == 0)
                    best_bid--;
            } else if (tick == best_ask) {
                while (best_ask <= MAX_ASK_TICK && qty_at[best_ask] == 0)
                    best_ask++;
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b{false, 0, 0, false, 0, 0};
        if (best_bid >= MIN_BID_TICK) {
            b.has_bid = true;
            b.bid_price = to_price(best_bid);
            b.bid_qty = qty_at[best_bid];
        }
        if (best_ask <= MAX_ASK_TICK) {
            b.has_ask = true;
            b.ask_price = to_price(best_ask);
            b.ask_qty = qty_at[best_ask];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (int t = best_bid; t >= MIN_BID_TICK && (int)out.size() < depth; t--)
            if (qty_at[t] != 0)
                out.push_back({to_price(t), qty_at[t]});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (int t = best_ask; t <= MAX_ASK_TICK && (int)out.size() < depth; t++)
            if (qty_at[t] != 0)
                out.push_back({to_price(t), qty_at[t]});
        return out;
    }

private:
    struct Order {
        int tick;
        long qty; // 0 means the slot is empty / cancelled
    };

    static const int NUM_TICKS = MAX_ASK_TICK + 1;

    // bid ticks and ask ticks never overlap, so one array holds both sides
    long qty_at[NUM_TICKS];
    int best_bid = MIN_BID_TICK - 1; // below range = no bids
    int best_ask = MAX_ASK_TICK + 1; // above range = no asks
    std::vector<Order> orders;
};
