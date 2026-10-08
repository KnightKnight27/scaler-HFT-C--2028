#pragma once

#include <cstdint>
#include <cmath>
#include <vector>
#include <string>

enum class Side {
    Buy,
    Sell
};

struct Level {
    double price;
    long qty;

    bool operator==(const Level& other) const {
        return std::abs(price - other.price) < 1e-6 && qty == other.qty;
    }
};

struct BBO {
    bool has_bid{false};
    double bid_price{0.0};
    long bid_qty{0};

    bool has_ask{false};
    double ask_price{0.0};
    long ask_qty{0};

    bool operator==(const BBO& other) const {
        if (has_bid != other.has_bid) return false;
        if (has_bid && (std::abs(bid_price - other.bid_price) > 1e-6 || bid_qty != other.bid_qty)) return false;
        if (has_ask != other.has_ask) return false;
        if (has_ask && (std::abs(ask_price - other.ask_price) > 1e-6 || ask_qty != other.ask_qty)) return false;
        return true;
    }
};

struct OrderRecord {
    uint64_t id;
    Side side;
    double price;
    long qty;
    int tick; // Precomputed tick index for O(1) structures
};

namespace Config {
    constexpr double MIN_BID_PRICE = 50.00;
    constexpr double MAX_BID_PRICE = 99.99;
    constexpr double MID_PRICE     = 100.00;
    constexpr double MIN_ASK_PRICE = 100.01;
    constexpr double MAX_ASK_PRICE = 150.00;
    constexpr double TICK_SIZE     = 0.01;

    constexpr int64_t MIN_CENTS = 5000;   // 50.00 * 100
    constexpr int64_t MAX_CENTS = 15000;  // 150.00 * 100
    constexpr int NUM_TICKS     = 10001;  // 15000 - 5000 + 1 = 10001 possible tick slots
}

inline int64_t price_to_cents(double price) {
    return static_cast<int64_t>(std::llround(price * 100.0));
}

inline double cents_to_price(int64_t cents) {
    return static_cast<double>(cents) / 100.0;
}

inline int cents_to_tick(int64_t cents) {
    return static_cast<int>(cents - Config::MIN_CENTS);
}

inline int64_t tick_to_cents(int tick) {
    return static_cast<int64_t>(tick + Config::MIN_CENTS);
}

inline int price_to_tick(double price) {
    return cents_to_tick(price_to_cents(price));
}

inline double tick_to_price(int tick) {
    return cents_to_price(tick_to_cents(tick));
}
