#include "orderbook_c.h"

using namespace std;

OrderBookC::OrderBookC() {
    bid_qty.assign(BAND, 0);
    ask_qty.assign(BAND, 0);
}

void OrderBookC::add_order(uint64_t id, Side side, double price, long qty) {
    long t = to_ticks(price);
    orders[id] = Info{side, t, qty};
    if (side == Side::Buy) {
        bid_qty[t - BID_LOW] += qty;
    } else {
        ask_qty[t - ASK_LOW] += qty;
    }
}

bool OrderBookC::cancel_order(uint64_t id) {
    auto it = orders.find(id);
    if (it == orders.end()) {
        return false;
    }
    if (it->second.side == Side::Buy) {
        bid_qty[it->second.tick - BID_LOW] -= it->second.qty;
    } else {
        ask_qty[it->second.tick - ASK_LOW] -= it->second.qty;
    }
    orders.erase(it);
    return true;
}

BBO OrderBookC::get_bbo() const {
    BBO b;
    // best bid = first non empty slot scanning down from the top of the band
    for (int i = BAND - 1; i >= 0; --i) {
        if (bid_qty[i] > 0) {
            b.has_bid = true;
            b.bid_price = to_price(i + BID_LOW);
            b.bid_qty = bid_qty[i];
            break;
        }
    }
    // best ask = first non empty slot scanning up from the bottom
    for (int i = 0; i < BAND; ++i) {
        if (ask_qty[i] > 0) {
            b.has_ask = true;
            b.ask_price = to_price(i + ASK_LOW);
            b.ask_qty = ask_qty[i];
            break;
        }
    }
    return b;
}

vector<Level> OrderBookC::get_bids(int depth) const {
    vector<Level> out;
    if (depth <= 0) return out;
    for (int i = BAND - 1; i >= 0 && (int)out.size() < depth; --i) {
        if (bid_qty[i] > 0) {
            out.push_back(Level{to_price(i + BID_LOW), bid_qty[i]});
        }
    }
    return out;
}

vector<Level> OrderBookC::get_asks(int depth) const {
    vector<Level> out;
    if (depth <= 0) return out;
    for (int i = 0; i < BAND && (int)out.size() < depth; ++i) {
        if (ask_qty[i] > 0) {
            out.push_back(Level{to_price(i + ASK_LOW), ask_qty[i]});
        }
    }
    return out;
}
