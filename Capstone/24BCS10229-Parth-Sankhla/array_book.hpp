#pragma once

#include "common.hpp"

// C: one array slot per price, orders looked up by id
class ArrayBook {
public:
    ArrayBook() : level(MAX_ASK + 1, 0), best_bid(0), best_ask(0), next_ts(0) {
        orders.reserve(1 << 20);
    }

    void add_order(uint64_t id, Side side, int price, long qty) {
        if (qty <= 0 || !valid_price(side, price))
            return;
        if (id >= orders.size())
            orders.resize(id * 2 + 1, {0, Side::Buy, 0, 0, 0});
        if (orders[id].qty != 0)
            return;

        orders[id] = {id, side, price, qty, next_ts++};
        level[price] += qty;

        if (side == Side::Buy) {
            if (best_bid == 0 || price > best_bid)
                best_bid = price;
        } else {
            if (best_ask == 0 || price < best_ask)
                best_ask = price;
        }
    }

    bool cancel_order(uint64_t id) {
        if (id >= orders.size() || orders[id].qty == 0)
            return false;

        Order o = orders[id];
        orders[id].qty = 0;
        level[o.price] -= o.qty;

        if (level[o.price] == 0) {
            if (o.side == Side::Buy && o.price == best_bid) {
                int p = best_bid - 1;
                while (p >= MIN_BID && level[p] == 0)
                    p--;
                best_bid = (p >= MIN_BID) ? p : 0;
            } else if (o.side == Side::Sell && o.price == best_ask) {
                int p = best_ask + 1;
                while (p <= MAX_ASK && level[p] == 0)
                    p++;
                best_ask = (p <= MAX_ASK) ? p : 0;
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b = {false, 0, 0, false, 0, 0};
        if (best_bid != 0) {
            b.has_bid = true;
            b.bid_price = best_bid;
            b.bid_qty = level[best_bid];
        }
        if (best_ask != 0) {
            b.has_ask = true;
            b.ask_price = best_ask;
            b.ask_qty = level[best_ask];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        if (best_bid == 0)
            return out;
        for (int p = best_bid; p >= MIN_BID && (int)out.size() < depth; p--)
            if (level[p] != 0)
                out.push_back({p, level[p]});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        if (best_ask == 0)
            return out;
        for (int p = best_ask; p <= MAX_ASK && (int)out.size() < depth; p++)
            if (level[p] != 0)
                out.push_back({p, level[p]});
        return out;
    }

private:
    std::vector<long> level;
    std::vector<Order> orders;
    int best_bid;
    int best_ask;
    uint64_t next_ts;
};
