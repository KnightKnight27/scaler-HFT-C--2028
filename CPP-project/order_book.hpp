#pragma once

#include <cstdint>
#include <cmath>
#include <vector>

using namespace std;

enum class Side {
    Buy,
    Sell
};

struct Level {
    double price{0.0};
    long qty{0};
};

struct BBO {
    bool has_bid{false};
    double bid_price{0.0};
    long bid_qty{0};

    bool has_ask{false};
    double ask_price{0.0};
    long ask_qty{0};
};

constexpr uint32_t MIN_PRICE_TICK = 5000;   // 50.00
constexpr uint32_t MAX_PRICE_TICK = 15000;  // 150.00
constexpr uint32_t MID_PRICE_TICK = 10000;  // 100.00
constexpr uint32_t TOTAL_PRICE_TICKS = 10001; // 5000 through 15000 inclusive

inline uint32_t price_to_tick(double price) {
    return static_cast<uint32_t>(lround(price * 100.0));
}

inline double tick_to_price(uint32_t tick) {
    return static_cast<double>(tick) / 100.0;
}

class OrderBook {
public:
    virtual ~OrderBook() = default;
    virtual void add_order(uint64_t id, Side side, double price, long qty) = 0;
    virtual bool cancel_order(uint64_t id) = 0;
    virtual BBO get_bbo() const = 0;
    virtual vector<Level> get_bids(int depth = 10) const = 0;
    virtual vector<Level> get_asks(int depth = 10) const = 0;
};
