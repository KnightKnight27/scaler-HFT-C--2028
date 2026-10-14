#pragma once

#include <cstdint>

enum class Side { Buy, Sell };

struct Level {
    double price{};
    long qty{};
};

struct BBO {
    bool has_bid{};
    double bid_price{};
    long bid_qty{};
    bool has_ask{};
    double ask_price{};
    long ask_qty{};
};

// All books use integer cents internally. Invalid prices, non-positive
// quantities, and duplicate IDs throw std::invalid_argument.
int price_to_ticks(double price, Side side);
double ticks_to_price(int ticks) noexcept;
