#ifndef ORDERBOOK_COMMON_HPP
#define ORDERBOOK_COMMON_HPP

#include <cmath>
#include <cstdint>
#include <vector>

enum class Side { Buy, Sell };

struct Level {
    double price;
    long qty;
};

struct BBO {
    bool has_bid = false;
    double bid_price = 0;
    long bid_qty = 0;

    bool has_ask = false;
    double ask_price = 0;
    long ask_qty = 0;
};

//inside the books prices are ticks of 0.01, so 99.98 is 9998
const int MIN_BID_TICK = 5000;
const int MAX_BID_TICK = 9999;
const int MIN_ASK_TICK = 10001;
const int MAX_ASK_TICK = 15000;

inline int to_tick(double price) {
    return (int)std::lround(price * 100);
}

inline double to_price(int tick) {
    return tick / 100.0;
}

inline bool operator==(const Level& a, const Level& b) {
    return a.price == b.price && a.qty == b.qty;
}

inline bool operator==(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.bid_price == b.bid_price && a.bid_qty == b.bid_qty &&
           a.has_ask == b.has_ask && a.ask_price == b.ask_price && a.ask_qty == b.ask_qty;
}

#endif
