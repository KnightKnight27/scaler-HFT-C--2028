// Shared types and price helpers used by all three order books.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

enum class Side { Buy, Sell };
struct Level { double price; long qty; };
struct BBO {
    bool has_bid;  double bid_price;  long bid_qty;
    bool has_ask;  double ask_price;  long ask_qty;
};

const int MIN_BID = 5000, MAX_BID = 9999;     // trick size: 50.00 .. 99.99
const int MIN_ASK = 10001, MAX_ASK = 15000;   // 100.01 .. 150.00

inline int to_tick(double price) {
    if (!(price > 0 && price < 1e6)) return -1;          // garbage input
    return (int)std::llround(price * 100.0);             // round, don't truncate (0.01*N is inexact)
}
inline double to_price(int tick) { return tick / 100.0; }

// Orders outside the legal price band or with qty <= 0 are ignored
inline bool valid(Side side, int tick, long qty) {
    if (qty <= 0) return false;
    if (side == Side::Buy) return tick >= MIN_BID && tick <= MAX_BID;
    return tick >= MIN_ASK && tick <= MAX_ASK;
}

inline BBO empty_bbo() { return BBO{false, 0, 0, false, 0, 0}; }

struct Order { long qty; int tick; Side side; };