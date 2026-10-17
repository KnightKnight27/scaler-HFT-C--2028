#pragma once
#include <vector>
#include "common.h"

const int NUM_TICKS = MAX_ASK + 1;

class BookC {
public:
    BookC() {
        levels.assign(NUM_TICKS, 0);
        best_bid = -1;
        best_ask = -1;
    }

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = to_tick(price);
        if (qty <= 0 || !good_tick(side, tick)) {
            return;
        }
        if (id < orders.size() && orders[id].qty > 0) {
            return;
        }

        if (id >= orders.size()) {
            orders.resize(id + 1, Order{0, Side::Buy, 0, 0});
        }
        orders[id] = Order{id, side, tick, qty};
        levels[tick] += qty;

        if (side == Side::Buy) {
            if (best_bid == -1 || tick > best_bid) {
                best_bid = tick;
            }
        } else {
            if (best_ask == -1 || tick < best_ask) {
                best_ask = tick;
            }
        }
    }

    bool cancel_order(uint64_t id) {
        if (id >= orders.size()) {
            return false;
        }
        if (orders[id].qty == 0) {
            return false;
        }

        Order o = orders[id];
        levels[o.tick] -= o.qty;
        orders[id].qty = 0;

        if (levels[o.tick] == 0) {
            if (o.side == Side::Buy && o.tick == best_bid) {
                int t = best_bid - 1;
                while (t >= MIN_BID && levels[t] == 0) {
                    t--;
                }
                if (t < MIN_BID) {
                    best_bid = -1;
                } else {
                    best_bid = t;
                }
            }
            if (o.side == Side::Sell && o.tick == best_ask) {
                int t = best_ask + 1;
                while (t <= MAX_ASK && levels[t] == 0) {
                    t++;
                }
                if (t > MAX_ASK) {
                    best_ask = -1;
                } else {
                    best_ask = t;
                }
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b = empty_bbo();
        if (best_bid != -1) {
            b.has_bid = true;
            b.bid_price = to_price(best_bid);
            b.bid_qty = levels[best_bid];
        }
        if (best_ask != -1) {
            b.has_ask = true;
            b.ask_price = to_price(best_ask);
            b.ask_qty = levels[best_ask];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        if (best_bid == -1) {
            return result;
        }
        for (int t = best_bid; t >= MIN_BID; t--) {
            if ((int)result.size() >= depth) {
                break;
            }
            if (levels[t] > 0) {
                result.push_back(Level{to_price(t), levels[t]});
            }
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        if (best_ask == -1) {
            return result;
        }
        for (int t = best_ask; t <= MAX_ASK; t++) {
            if ((int)result.size() >= depth) {
                break;
            }
            if (levels[t] > 0) {
                result.push_back(Level{to_price(t), levels[t]});
            }
        }
        return result;
    }

private:
    std::vector<long> levels;
    std::vector<Order> orders;
    int best_bid;
    int best_ask;
};
