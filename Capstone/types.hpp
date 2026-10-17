#pragma once
#include <cstdint>
enum class Side { Buy, Sell };

const int MIN_BID = 5000;
const int MAX_BID = 9999;
const int MIN_ASK = 10001;
const int MAX_ASK = 15000;
const int TOTAL_TICKS = 10001;

struct Order {
    uint64_t id;
    Side side;
    uint32_t price;
    long qty;
};

struct Level {
    double price;
    long qty;
};

struct BBO {
    bool has_bid;
    uint32_t bid_price;
    long bid_qty;

    bool has_ask;
    uint32_t ask_price;
    long ask_qty;
};
