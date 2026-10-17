#ifndef ORDER_H
#define ORDER_H

#include <cstdint>
#include <cmath>
#include <vector>

enum class Side { Buy, Sell };

struct Level {
    double price{0.0};
    long qty{0};
};

struct BBO {
    Level bid_level{};
    Level ask_level{};
    bool has_bid{false};
    bool has_ask{false};
};

class IOrderBook {
public:
    virtual ~IOrderBook() = default;
    virtual void add_order(uint64_t id, Side side, double price, long qty) = 0;
    virtual bool cancel_order(uint64_t id) = 0;
    virtual BBO get_bbo() const = 0;
    virtual std::vector<Level> get_bids(int depth = 10) const = 0;
    virtual std::vector<Level> get_asks(int depth = 10) const = 0;
};

inline long price_to_ticks(double price) {
    return std::lround(price * 100.0);
}

inline double ticks_to_price(long ticks) {
    return static_cast<double>(ticks) / 100.0;
}

inline double normalize_price(double price) {
    return ticks_to_price(price_to_ticks(price));
}

#endif // ORDER_H
