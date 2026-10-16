#pragma once

#include <cstdint>
#include <vector>
#include <cmath>
#include <string>

// Order side
enum class Side { 
    Buy, 
    Sell 
};

// Price level reporting
struct Level {
    double price;
    long qty;

    bool operator==(const Level& other) const {
        return std::abs(price - other.price) < 1e-5 && qty == other.qty;
    }
};

// Best Bid and Offer (BBO)
struct BBO {
    bool has_bid;
    double bid_price;
    long bid_qty;

    bool has_ask;
    double ask_price;
    long ask_qty;

    bool operator==(const BBO& other) const {
        if (has_bid != other.has_bid || has_ask != other.has_ask) return false;
        if (has_bid) {
            if (std::abs(bid_price - other.bid_price) >= 1e-5 || bid_qty != other.bid_qty) return false;
        }
        if (has_ask) {
            if (std::abs(ask_price - other.ask_price) >= 1e-5 || ask_qty != other.ask_qty) return false;
        }
        return true;
    }
};

// Common OrderBook Interface
class OrderBook {
public:
    virtual ~OrderBook() = default;

    virtual void add_order(uint64_t id, Side side, double price, long qty) = 0;
    virtual bool cancel_order(uint64_t id) = 0;
    virtual BBO get_bbo() const = 0;
    virtual std::vector<Level> get_bids(int depth = 10) const = 0;
    virtual std::vector<Level> get_asks(int depth = 10) const = 0;
};

// Tick conversion helpers based on instrument rules:
// Tick size: 0.01
// Bid price range: [50.00, 99.99]
// Ask price range: [100.01, 150.00]
// Mid price: 100.00
// Base price: 50.00, Max price: 150.00 -> 10,001 ticks (indices 0 to 10000)
inline int price_to_tick(double price) {
    return static_cast<int>(std::round((price - 50.00) * 100.0));
}

inline double tick_to_price(int tick) {
    return 50.00 + (static_cast<double>(tick) * 0.01);
}

inline double normalize_price(double price) {
    return std::round(price * 100.0) / 100.0;
}
