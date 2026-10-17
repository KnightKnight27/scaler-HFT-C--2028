#pragma once

#include <vector>

enum class Side
{
    Buy,
    Sell
};

struct Level
{
    double price;
    long qty;
};

struct BBO
{
    bool has_bid = false;
    double bid_price = 0.0;
    long bid_qty = 0;

    bool has_ask = false;
    double ask_price = 0.0;
    long ask_qty = 0;
};

inline int priceToTicks(double price)
{
    return static_cast<int>(price * 100.0 + (price >= 0 ? 0.5 : -0.5));
}

inline double ticksToPrice(int ticks)
{
    return static_cast<double>(ticks) / 100.0;
}
