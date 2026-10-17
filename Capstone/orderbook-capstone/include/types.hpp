#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

enum class Side { Buy, Sell };

struct Level {
    double price;
    long qty;

    bool operator==(const Level& other) const noexcept {
        return std::llround(price * 100.0) == std::llround(other.price * 100.0) &&
               qty == other.qty;
    }
};

struct BBO {
    bool has_bid;
    double bid_price;
    long bid_qty;
    bool has_ask;
    double ask_price;
    long ask_qty;

    bool operator==(const BBO& other) const noexcept {
        if (has_bid != other.has_bid || has_ask != other.has_ask) return false;
        if (has_bid) {
            if (std::llround(bid_price * 100.0) != std::llround(other.bid_price * 100.0) ||
                bid_qty != other.bid_qty) {
                return false;
            }
        }
        if (has_ask) {
            if (std::llround(ask_price * 100.0) != std::llround(other.ask_price * 100.0) ||
                ask_qty != other.ask_qty) {
                return false;
            }
        }
        return true;
    }
};

// Tick conversion constants:
// Bid range: [50.00, 99.99] -> ticks [5000, 9999] (5,000 levels)
// Ask range: [100.01, 150.00] -> ticks [10001, 15000] (5,000 levels)
constexpr int32_t MIN_BID_TICK = 5000;
constexpr int32_t MAX_BID_TICK = 9999;
constexpr int32_t MIN_ASK_TICK = 10001;
constexpr int32_t MAX_ASK_TICK = 15000;
constexpr int32_t NUM_LEVELS_PER_SIDE = 5000;

inline int32_t price_to_tick(double price) noexcept {
    return static_cast<int32_t>(std::llround(price * 100.0));
}

inline double tick_to_price(int32_t tick) noexcept {
    return static_cast<double>(tick) * 0.01;
}