#ifndef ORDER_H
#define ORDER_H

#include <cstdint>
#include <cmath>
#include <vector>

enum class Side { Buy, Sell };

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

// Common base class so the benchmark can drive all 3 impls with one pointer.
class OrderBook {
public:
    virtual ~OrderBook() {}
    virtual void add_order(uint64_t id, Side side, double price, long qty) = 0;
    virtual bool cancel_order(uint64_t id) = 0;
    virtual BBO get_bbo() const = 0;
    virtual std::vector<Level> get_bids(int depth = 10) const = 0;
    virtual std::vector<Level> get_asks(int depth = 10) const = 0;
};

// Inside the book prices are kept as integer ticks (price * 100) so we don't
// compare doubles. 100.01 -> 10001 etc.
inline long to_ticks(double p) {
    return (long)std::lround(p * 100.0);
}

inline double to_price(long t) {
    return (double)t / 100.0;
}

#endif
