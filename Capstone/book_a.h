#pragma once
#include <map>
#include <unordered_map>
#include <functional>
#include "common.h"

class BookA {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = to_tick(price);
        if (qty <= 0 || !good_tick(side, tick)) {
            return;
        }
        if (orders.count(id) > 0) {
            return;
        }

        orders[id] = Order{id, side, tick, qty};

        if (side == Side::Buy) {
            bids[tick] += qty;
        } else {
            asks[tick] += qty;
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = orders.find(id);
        if (it == orders.end()) {
            return false;
        }

        Order o = it->second;
        if (o.side == Side::Buy) {
            bids[o.tick] -= o.qty;
            if (bids[o.tick] == 0) {
                bids.erase(o.tick);
            }
        } else {
            asks[o.tick] -= o.qty;
            if (asks[o.tick] == 0) {
                asks.erase(o.tick);
            }
        }

        orders.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO b = empty_bbo();
        if (!bids.empty()) {
            b.has_bid = true;
            b.bid_price = to_price(bids.begin()->first);
            b.bid_qty = bids.begin()->second;
        }
        if (!asks.empty()) {
            b.has_ask = true;
            b.ask_price = to_price(asks.begin()->first);
            b.ask_qty = asks.begin()->second;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        for (auto it = bids.begin(); it != bids.end(); it++) {
            if ((int)result.size() >= depth) {
                break;
            }
            result.push_back(Level{to_price(it->first), it->second});
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        for (auto it = asks.begin(); it != asks.end(); it++) {
            if ((int)result.size() >= depth) {
                break;
            }
            result.push_back(Level{to_price(it->first), it->second});
        }
        return result;
    }

private:
    std::map<int, long, std::greater<int>> bids;
    std::map<int, long> asks;
    std::unordered_map<uint64_t, Order> orders;
};
