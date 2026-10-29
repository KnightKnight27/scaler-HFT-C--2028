#pragma once

#include <cmath>
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

namespace detail {
    constexpr int32_t MIN_BID = 5000;
    constexpr int32_t MAX_BID = 9999;
    constexpr int32_t MID = 10000;
    constexpr int32_t MIN_ASK = 10001;
    constexpr int32_t MAX_ASK = 15000;
    constexpr int32_t TOTAL  = 10001;

    inline int32_t price_to_int32(double price) {
        return static_cast<int32_t>(std::llround(price * 100.0));
    }

    inline double int32_to_price(int32_t int32_t) {
        return static_cast<double>(int32_t) * 0.01;
    }
}