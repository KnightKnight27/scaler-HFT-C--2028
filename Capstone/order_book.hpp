#ifndef ORDER_BOOK_HPP
#define ORDER_BOOK_HPP

#include <cstdint>
#include <vector>

namespace capstone {

enum class Side {
    Buy,
    Sell
};

// Prices are stored as integer ticks.
// Example: price 100.50 with tick size 0.01 is 10050 ticks.
using Price = int;

struct Order {
    std::uint64_t id;
    Side side;
    Price priceTicks;
    long qty;
    std::uint64_t timestampNs;
};

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

class OrderBook {
public:
    virtual ~OrderBook() = default;

    virtual void addOrder(const Order& order) = 0;
    virtual bool removeOrder(std::uint64_t id) = 0;

    virtual BBO getBBO() const = 0;
    virtual std::vector<Level> getBids(int depth = 10) const = 0;
    virtual std::vector<Level> getAsks(int depth = 10) const = 0;
};

} // namespace capstone

#endif
