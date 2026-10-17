#pragma once
#include <map>
#include <vector>
#include "common.h"

struct PriceLevel {
    int tick;
    long qty;
};

class BookB {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = to_tick(price);
        if (qty <= 0 || !good_tick(side, tick)) {
            return;
        }
        if (orders.find(id) != orders.end()) {
            return;
        }

        orders[id] = Order{id, side, tick, qty};

        if (side == Side::Buy) {
            add_to(bids, tick, qty, true);
        } else {
            add_to(asks, tick, qty, false);
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = orders.find(id);
        if (it == orders.end()) {
            return false;
        }

        Order o = it->second;
        if (o.side == Side::Buy) {
            remove_from(bids, o.tick, o.qty, true);
        } else {
            remove_from(asks, o.tick, o.qty, false);
        }

        orders.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO b = empty_bbo();
        if (!bids.empty()) {
            b.has_bid = true;
            b.bid_price = to_price(bids.back().tick);
            b.bid_qty = bids.back().qty;
        }
        if (!asks.empty()) {
            b.has_ask = true;
            b.ask_price = to_price(asks.back().tick);
            b.ask_qty = asks.back().qty;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        return read_levels(bids, depth);
    }

    std::vector<Level> get_asks(int depth = 10) const {
        return read_levels(asks, depth);
    }

private:
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
    std::map<uint64_t, Order> orders;

    int find_pos(const std::vector<PriceLevel>& v, int tick, bool up) const {
        int lo = 0;
        int hi = v.size();
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            bool before;
            if (up) {
                before = v[mid].tick < tick;
            } else {
                before = v[mid].tick > tick;
            }
            if (before) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo;
    }

    void add_to(std::vector<PriceLevel>& v, int tick, long qty, bool up) {
        int pos = find_pos(v, tick, up);
        if (pos < (int)v.size() && v[pos].tick == tick) {
            v[pos].qty += qty;
        } else {
            v.insert(v.begin() + pos, PriceLevel{tick, qty});
        }
    }

    void remove_from(std::vector<PriceLevel>& v, int tick, long qty, bool up) {
        int pos = find_pos(v, tick, up);
        if (pos >= (int)v.size() || v[pos].tick != tick) {
            return;
        }
        v[pos].qty -= qty;
        if (v[pos].qty == 0) {
            v.erase(v.begin() + pos);
        }
    }

    std::vector<Level> read_levels(const std::vector<PriceLevel>& v, int depth) const {
        std::vector<Level> result;
        for (int i = (int)v.size() - 1; i >= 0; i--) {
            if ((int)result.size() >= depth) {
                break;
            }
            result.push_back(Level{to_price(v[i].tick), v[i].qty});
        }
        return result;
    }
};
