#pragma once

#include "common.hpp"

// Implementation C: one array slot per price, and orders stored in a vector
// where the index is the order id.
class ArrayBook {
public:
    ArrayBook() : bid_qty(LEVELS_PER_SIDE, 0), ask_qty(LEVELS_PER_SIDE, 0) {
        orders.reserve(1 << 20);
    }

    void add_order(uint64_t id, Side side, Price price, long qty) {
        if (!valid_order(side, price, qty)) return;
        if (id >= orders.size()) orders.resize(id + 1, {Side::Buy, 0, 0});
        if (orders[id].qty > 0) return;

        orders[id] = {side, price, qty};
        if (side == Side::Buy) {
            int i = price - BID_LOW;
            bid_qty[i] += qty;
            if (i > best_bid) best_bid = i;
        } else {
            int i = price - ASK_LOW;
            ask_qty[i] += qty;
            if (i < best_ask) best_ask = i;
        }
    }

    bool cancel_order(uint64_t id) {
        if (id >= orders.size() || orders[id].qty == 0) return false;

        Order& o = orders[id];
        if (o.side == Side::Buy) {
            int i = o.price - BID_LOW;
            bid_qty[i] -= o.qty;
            // best level is empty now, walk down to the next one
            if (i == best_bid) {
                while (best_bid >= 0 && bid_qty[best_bid] == 0) best_bid--;
            }
        } else {
            int i = o.price - ASK_LOW;
            ask_qty[i] -= o.qty;
            if (i == best_ask) {
                while (best_ask < LEVELS_PER_SIDE && ask_qty[best_ask] == 0) best_ask++;
            }
        }
        o.qty = 0;
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (best_bid >= 0) {
            b.has_bid = true;
            b.bid_price = BID_LOW + best_bid;
            b.bid_qty = bid_qty[best_bid];
        }
        if (best_ask < LEVELS_PER_SIDE) {
            b.has_ask = true;
            b.ask_price = ASK_LOW + best_ask;
            b.ask_qty = ask_qty[best_ask];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (int i = best_bid; i >= 0 && (int)out.size() < depth; i--) {
            if (bid_qty[i] > 0) out.push_back({BID_LOW + i, bid_qty[i]});
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (int i = best_ask; i < LEVELS_PER_SIDE && (int)out.size() < depth; i++) {
            if (ask_qty[i] > 0) out.push_back({ASK_LOW + i, ask_qty[i]});
        }
        return out;
    }

private:
    std::vector<long> bid_qty;  // index 0 is 50.00, index 4999 is 99.99
    std::vector<long> ask_qty;  // index 0 is 100.01, index 4999 is 150.00
    int best_bid = -1;               // -1 means no bids
    int best_ask = LEVELS_PER_SIDE;  // 5000 means no asks
    std::vector<Order> orders;       // qty 0 means not live
};
