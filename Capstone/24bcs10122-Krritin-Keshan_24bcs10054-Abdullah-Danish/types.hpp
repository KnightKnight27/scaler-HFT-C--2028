#pragma once

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

// prices are kept as ticks of 0.01, 99.99 -> 9999
const int MIN_BID = 5000;
const int MAX_BID = 9999;
const int MIN_ASK = 10001;
const int MAX_ASK = 15000;

inline int toTick(double price) { return (int)(price * 100 + 0.5); }
inline double toPrice(int tick) { return tick / 100.0; }

inline bool isValid(Side side, int tick, long qty) {
    if (qty <= 0) return false;
    if (side == Side::Buy) return tick >= MIN_BID && tick <= MAX_BID;
    return tick >= MIN_ASK && tick <= MAX_ASK;
}
