#pragma once
#include <cstdint>
#include <cmath>
#include <vector>

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

struct Order {
    uint64_t id;
    Side side;
    int tick;
    long qty;
};

const int MIN_BID = 5000;
const int MAX_BID = 9999;
const int MIN_ASK = 10001;
const int MAX_ASK = 15000;

inline int to_tick(double price) {
    return (int)std::llround(price * 100.0);
}

inline double to_price(int tick) {
    return tick / 100.0;
}

inline bool good_tick(Side side, int tick) {
    if (side == Side::Buy) {
        return tick >= MIN_BID && tick <= MAX_BID;
    }
    return tick >= MIN_ASK && tick <= MAX_ASK;
}

inline BBO empty_bbo() {
    BBO b;
    b.has_bid = false;
    b.bid_price = 0;
    b.bid_qty = 0;
    b.has_ask = false;
    b.ask_price = 0;
    b.ask_qty = 0;
    return b;
}
