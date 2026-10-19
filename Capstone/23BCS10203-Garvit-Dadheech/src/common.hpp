// Types shared by the three implementations.  The API is the one from the
// assignment (double price, long qty); internally everything works on integer
// ticks (price * 100).
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

// -DOB_PROFILE keeps the public methods out of line so perf report can show
// them by name.  Normal builds inline as usual.
#ifdef OB_PROFILE
#define OB_API __attribute__((noinline))
#else
#define OB_API
#endif

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

namespace ob {

// Tick size is 0.01, so one price unit is 100 ticks.
constexpr int kTicksPerUnit = 100;

// Bid band  [50.00, 99.99]  -> ticks [5000,  9999]
// Ask band  [100.01, 150.00] -> ticks [10001, 15000]
constexpr int kBidMinTick = 5000;
constexpr int kBidMaxTick = 9999;
constexpr int kAskMinTick = 10001;
constexpr int kAskMaxTick = 15000;

inline int to_tick(double price) {
    return static_cast<int>(std::llround(price * kTicksPerUnit));
}

inline double to_price(int tick) {
    return static_cast<double>(tick) / kTicksPerUnit;
}

inline BBO empty_bbo() {
    return BBO{false, 0.0, 0, false, 0.0, 0};
}

}  // namespace ob
