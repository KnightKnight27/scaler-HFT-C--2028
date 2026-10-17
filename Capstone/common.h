#pragma once
#include <bits/stdc++.h>
using namespace std;

// stuff that all 3 order books use

enum class Side { Buy, Sell };

struct Level {
    double price;
    long qty;
};

struct BBO {
    bool has_bid;
    double bid_price;
    long bid_qty;

    bool has_ask;
    double ask_price;
    long ask_qty;
};

// inside the books prices are kept as ticks (int), 1 tick = 0.01
// 99.99 -> 9999, 100.01 -> 10001
#define MIN_BID 5000
#define MAX_BID 9999
#define MIN_ASK 10001
#define MAX_ASK 15000

inline int toTicks(double price) {
    return (int)(price * 100.0 + 0.5); // +0.5 so 99.99 doesnt become 9998
}

inline double toPrice(int ticks) {
    return ticks / 100.0;
}

// is this price allowed for this side
inline bool priceOk(Side side, int t) {
    if (side == Side::Buy) return t >= MIN_BID && t <= MAX_BID;
    return t >= MIN_ASK && t <= MAX_ASK;
}

inline BBO emptyBBO() {
    BBO b;
    b.has_bid = false;
    b.bid_price = 0;
    b.bid_qty = 0;
    b.has_ask = false;
    b.ask_price = 0;
    b.ask_qty = 0;
    return b;
}

// all 3 books inherit from this so the benchmark can use any of them
class OrderBookBase {
public:
    virtual ~OrderBookBase() {}
    virtual void add_order(uint64_t id, Side side, double price, long qty) = 0;
    virtual bool cancel_order(uint64_t id) = 0;
    virtual BBO get_bbo() const = 0;
    virtual vector<Level> get_bids(int depth = 10) const = 0;
    virtual vector<Level> get_asks(int depth = 10) const = 0;
};
