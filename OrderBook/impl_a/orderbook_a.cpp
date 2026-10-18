#include "orderbook_a.h"

using namespace std;

void OrderBookA::add_order(uint64_t id, Side side, double price, long qty) {
    long t = to_ticks(price);
    orders[id] = Info{side, t, qty};
    if (side == Side::Buy) {
        bid_levels[t] += qty;   // adds a new map node or bumps the qty
    } else {
        ask_levels[t] += qty;
    }
}

bool OrderBookA::cancel_order(uint64_t id) {
    auto it = orders.find(id);
    if (it == orders.end()) {
        return false;   // unknown or already cancelled
    }
    if (it->second.side == Side::Buy) {
        auto lv = bid_levels.find(it->second.tick);
        if (lv != bid_levels.end()) {
            lv->second -= it->second.qty;
            if (lv->second <= 0) {
                bid_levels.erase(lv);
            }
        }
    } else {
        auto lv = ask_levels.find(it->second.tick);
        if (lv != ask_levels.end()) {
            lv->second -= it->second.qty;
            if (lv->second <= 0) {
                ask_levels.erase(lv);
            }
        }
    }
    orders.erase(it);
    return true;
}

BBO OrderBookA::get_bbo() const {
    BBO b;
    if (!bid_levels.empty()) {
        b.has_bid = true;
        b.bid_price = to_price(bid_levels.begin()->first);   // begin = highest bid
        b.bid_qty = bid_levels.begin()->second;
    }
    if (!ask_levels.empty()) {
        b.has_ask = true;
        b.ask_price = to_price(ask_levels.begin()->first);   // begin = lowest ask
        b.ask_qty = ask_levels.begin()->second;
    }
    return b;
}

vector<Level> OrderBookA::get_bids(int depth) const {
    vector<Level> out;
    if (depth <= 0) return out;
    int n = 0;
    for (auto it = bid_levels.begin(); it != bid_levels.end() && n < depth; ++it) {
        out.push_back(Level{to_price(it->first), it->second});
        n++;
    }
    return out;
}

vector<Level> OrderBookA::get_asks(int depth) const {
    vector<Level> out;
    if (depth <= 0) return out;
    int n = 0;
    for (auto it = ask_levels.begin(); it != ask_levels.end() && n < depth; ++it) {
        out.push_back(Level{to_price(it->first), it->second});
        n++;
    }
    return out;
}
