#pragma once

#include <cstdint>
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

constexpr int TICK_MULTIPLIER = 100;
constexpr int MIN_BID_TICK = 5000;
constexpr int MAX_BID_TICK = 9999;
constexpr int MIN_ASK_TICK = 10001;
constexpr int MAX_ASK_TICK = 15000;
constexpr int MIN_TICK = 5000;
constexpr int MAX_TICK = 15000;
constexpr int NUM_TICKS = MAX_TICK - MIN_TICK + 1;

inline int price_to_tick(double price) {
    return static_cast<int>(price * TICK_MULTIPLIER + 0.5);
}

inline double tick_to_price(int tick) {
    return tick * 0.01;
}

struct Order {
    uint64_t id;
    Side side;
    int tick;
    long qty;
};
