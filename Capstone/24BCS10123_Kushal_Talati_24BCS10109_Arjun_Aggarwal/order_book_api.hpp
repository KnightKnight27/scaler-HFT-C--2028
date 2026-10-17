#pragma once
// common types + constants shared by all three order books
#include <cstdint>
#include <cmath>
#include <vector>

enum class Side : uint8_t { Buy, Sell };

struct Level {
    double price;
    long qty;
};

struct BBO {
    bool has_bid = false;
    double bid_price = 0.0;
    long bid_qty = 0;
    bool has_ask = false;
    double ask_price = 0.0;
    long ask_qty = 0;
};

// prices are stored as integer ticks (0.01) internally, doubles only at the api edge
constexpr int32_t TICK_PER_UNIT = 100;
constexpr int32_t BID_MIN_TICK = 5000;   // 50.00
constexpr int32_t BID_MAX_TICK = 9999;   // 99.99
constexpr int32_t ASK_MIN_TICK = 10001;  // 100.01
constexpr int32_t ASK_MAX_TICK = 15000;  // 150.00
constexpr int LEVELS_PER_SIDE = 5000;

inline int32_t to_tick(double price) {
    return static_cast<int32_t>(std::llround(price * TICK_PER_UNIT));
}

inline double to_price(int32_t tick) {
    return tick / 100.0;
}

inline bool tick_in_band(Side side, int32_t tick) {
    if (side == Side::Buy) return tick >= BID_MIN_TICK && tick <= BID_MAX_TICK;
    return tick >= ASK_MIN_TICK && tick <= ASK_MAX_TICK;
}

// what we remember about a resting order. live is only used by impl C
struct OrderRec {
    int32_t tick;
    int32_t qty;
    Side side;
    bool live;
};

/*
every implementation exposes exactly this:
    bool add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO  get_bbo();
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;
add returns false (and does nothing) for out of band price, qty <= 0 or a reused id.
*/
