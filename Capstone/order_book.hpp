#pragma once

#include <cstdint>
#include <vector>
#include <cmath>

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
        if (has_bid != other.has_bid || has_ask != other.has_ask) return false;
        if (has_bid && (std::abs(bid_price - other.bid_price) > 1e-6 || bid_qty != other.bid_qty)) return false;
        if (has_ask && (std::abs(ask_price - other.ask_price) > 1e-6 || ask_qty != other.ask_qty)) return false;
        return true;
    }
};

// Base class interface for all three order book implementations
class IOrderBook {
public:
    virtual ~IOrderBook() = default;

    virtual void add_order(uint64_t id, Side side, double price, long qty) = 0;
    virtual bool cancel_order(uint64_t id) = 0;
    virtual BBO get_bbo() const = 0;
    virtual std::vector<Level> get_bids(int depth = 10) const = 0;
    virtual std::vector<Level> get_asks(int depth = 10) const = 0;
};
