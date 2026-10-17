#pragma once

#include <cstdint>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace orderbook {

enum class Side { Buy, Sell };
using Tick = int;

struct Level {
    double price{};
    long qty{};
    bool operator==(const Level& other) const { return price == other.price && qty == other.qty; }
};

struct BBO {
    bool has_bid{};
    double bid_price{};
    long bid_qty{};
    bool has_ask{};
    double ask_price{};
    long ask_qty{};
    bool operator==(const BBO& other) const {
        return has_bid == other.has_bid && bid_price == other.bid_price && bid_qty == other.bid_qty &&
               has_ask == other.has_ask && ask_price == other.ask_price && ask_qty == other.ask_qty;
    }
};

struct Order {
    Side side;
    Tick tick;
    long qty;
};

constexpr Tick kMinBidTick = 5000;
constexpr Tick kMaxBidTick = 9999;
constexpr Tick kMinAskTick = 10001;
constexpr Tick kMaxAskTick = 15000;
constexpr int kTickCount = kMaxAskTick + 1;

inline Tick to_tick(double price) {
    if (!std::isfinite(price)) {
        throw std::invalid_argument("price must be finite");
    }
    const auto tick = static_cast<Tick>(std::llround(price * 100.0));
    if (std::abs(price - static_cast<double>(tick) / 100.0) > 1e-9) {
        throw std::invalid_argument("price must be a multiple of 0.01");
    }
    return tick;
}

inline double to_price(Tick tick) { return static_cast<double>(tick) / 100.0; }

inline void validate_order(Side side, Tick tick, long qty) {
    if (qty <= 0) {
        throw std::invalid_argument("quantity must be positive");
    }
    if (side == Side::Buy && (tick < kMinBidTick || tick > kMaxBidTick)) {
        throw std::invalid_argument("buy price is outside [50.00, 99.99]");
    }
    if (side == Side::Sell && (tick < kMinAskTick || tick > kMaxAskTick)) {
        throw std::invalid_argument("sell price is outside [100.01, 150.00]");
    }
}

class OrderBook {
public:
    virtual ~OrderBook() = default;
    virtual void add_order(std::uint64_t id, Side side, double price, long qty) = 0;
    virtual bool cancel_order(std::uint64_t id) = 0;
    virtual BBO get_bbo() const = 0;
    virtual std::vector<Level> get_bids(int depth = 10) const = 0;
    virtual std::vector<Level> get_asks(int depth = 10) const = 0;
};

inline std::string implementation_name(char impl) {
    switch (impl) {
    case 'A': return "A (ordered maps)";
    case 'B': return "B (lazy heaps)";
    case 'C': return "C (indexed arrays)";
    default: throw std::invalid_argument("implementation must be A, B, or C");
    }
}

} // namespace orderbook
