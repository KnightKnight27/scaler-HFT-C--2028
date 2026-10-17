#ifndef ORDERBOOK_H
#define ORDERBOOK_H

#include <cstdint>
#include <vector>
#include <cmath>

enum class Side { Buy, Sell };

struct Level {
    double price;
    long qty;
};

struct BBO {
    bool has_bid;
    double bid_price;
    long bid_qty;
    bool has_ask;
    double ask_price;
    long ask_qty;
};

#define BID_MIN_TICK 5000
#define BID_MAX_TICK 9999
#define ASK_MIN_TICK 10001
#define ASK_MAX_TICK 15000

static inline int price_to_tick(double price) {
    return (int)llround(price * 100.0);
}
static inline double tick_to_price(int tick) {
    return tick / 100.0;
}

struct OrderRec {
    Side side;
    int tick;
    long qty;
};

class IOrderBook {
public:
    virtual ~IOrderBook() {}
    virtual void add_order(uint64_t id, Side side, double price, long qty) = 0;
    virtual bool cancel_order(uint64_t id) = 0;
    virtual BBO get_bbo() const = 0;
    virtual std::vector<Level> get_bids(int depth = 10) const = 0;
    virtual std::vector<Level> get_asks(int depth = 10) const = 0;
};

#endif
