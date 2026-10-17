#pragma once

#include <cstdint>
#include <vector>
#include <cmath>

// Order side: Buy or Sell
enum class Side {
    Buy,
    Sell
};

// Represents a price level with total resting quantity
struct Level {
    double price;
    long qty;
};

// Best Bid and Offer (BBO) structure
struct BBO {
    bool has_bid;
    double bid_price;
    long bid_qty;

    bool has_ask;
    double ask_price;
    long ask_qty;
};

// Price bounds as specified in assignment:
// Tick size: 0.01
// Bid range: [50.00, 99.99]  -> [5000, 9999] cents
// Ask range: [100.01, 150.00] -> [10001, 15000] cents
// Total possible price levels: 10,000
namespace Config {
    constexpr int MIN_BID_CENTS = 5000;
    constexpr int MAX_BID_CENTS = 9999;
    constexpr int MID_CENTS     = 10000;
    constexpr int MIN_ASK_CENTS = 10001;
    constexpr int MAX_ASK_CENTS = 15000;

    constexpr int MIN_CENTS = 5000;
    constexpr int MAX_CENTS = 15000;
    constexpr int TOTAL_TICKS = MAX_CENTS - MIN_CENTS + 1; // 10001 slots
}

// Convert floating-point price to integer cents
inline int64_t to_cents(double price) {
    return static_cast<int64_t>(price * 100.0 + 0.5);
}

// Convert integer cents back to double price
inline double to_double(int64_t cents) {
    return static_cast<double>(cents) / 100.0;
}
