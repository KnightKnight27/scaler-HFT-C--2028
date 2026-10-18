#pragma once

#include <cstdint>
#include <vector>

// prices in ticks, 100.01 -> 10001
const int MIN_BID = 5000;
const int MAX_BID = 9999;
const int MIN_ASK = 10001;
const int MAX_ASK = 15000;

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
    uint64_t id;
    Side side;
    int price;
    long qty;
    uint64_t ts;
};

inline bool valid_price(Side side, int price) {
    if (side == Side::Buy)
        return price >= MIN_BID && price <= MAX_BID;
    return price >= MIN_ASK && price <= MAX_ASK;
}
