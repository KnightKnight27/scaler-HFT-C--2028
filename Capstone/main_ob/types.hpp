#pragma once

#include <cstdint>
#include <cmath>

namespace ob {

using Tick    = int32_t;
using Qty     = int64_t;
using OrderId = uint64_t;

enum class Side : uint8_t {
    Buy  = 0,
    Sell = 1,
};

constexpr Tick MIN_TICK    = 5000;
constexpr Tick MAX_TICK    = 15000;
constexpr Tick BID_LO_TICK = 5000;
constexpr Tick BID_HI_TICK = 9999;
constexpr Tick ASK_LO_TICK = 10001;
constexpr Tick ASK_HI_TICK = 15000;

constexpr int NUM_TICKS = MAX_TICK - MIN_TICK + 1;

inline Tick to_tick(double price) {
    return static_cast<Tick>(std::llround(price * 100.0));
}

inline double to_price(Tick t) {
    return static_cast<double>(t) / 100.0;
}

struct Level {
    Tick price;
    Qty  qty;

    bool operator==(const Level& other) const {
        return price == other.price && qty == other.qty;
    }
};

struct BBO {
    bool has_bid;
    Tick bid_price;
    Qty  bid_qty;

    bool has_ask;
    Tick ask_price;
    Qty  ask_qty;

    bool operator==(const BBO& other) const {
        return has_bid == other.has_bid &&
               bid_price == other.bid_price &&
               bid_qty   == other.bid_qty &&
               has_ask == other.has_ask &&
               ask_price == other.ask_price &&
               ask_qty   == other.ask_qty;
    }
};

static_assert(sizeof(Side) == 1, "Side must be 1 byte");
static_assert(NUM_TICKS == 10001, "flat array must cover 5000..15000 inclusive");

} // namespace ob