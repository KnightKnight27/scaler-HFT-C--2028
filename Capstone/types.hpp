#pragma once
#include <cstdint>

// Shared by all three order books and the benchmark.
// Prices are integer ticks: 1 tick = 0.01, so 100.01 is stored as 10001.

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
    long qty;
    int price;
    Side side;
};