#pragma once
#include <cstdint>

constexpr int BID_BASE = 5000;
constexpr int BID_MAX = 9999;
constexpr int ASK_BASE = 10001;
constexpr int ASK_MAX = 15000;
constexpr int LEVELS_PER_SIDE = 5000;
constexpr int MAX_ID = 1 << 21;

enum class Side { Buy, Sell };

struct Level {
    int price;
    long qty;
};

struct BBO {
    bool has_bid;
    int bid_price;
    long bid_qty;

    bool has_ask;
    int ask_price;
    long ask_qty;
};

struct Order {
    Side side;
    int price;
    long qty;
};

inline double to_price(int ticks) { return ticks / 100.0; }
