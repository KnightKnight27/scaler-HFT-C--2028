#pragma once
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// Tick arithmetic
// ---------------------------------------------------------------------------
// Tick size: 0.01  (prices are always multiples of 0.01)
// We store prices as integer ticks to avoid floating-point drift.
//
// Bid range : [50.00, 99.99]   → ticks [5000, 9999]
// Ask range : [100.01, 150.00] → ticks [10001, 15000]
// Mid       :  100.00          → tick  10000  (never a resting level)
//
// Flat slot index:
//   slot = tick - TICK_MIN   (0 … 10000, but slot 5000 = mid is unused)
// ---------------------------------------------------------------------------

static constexpr int TICK_MIN     = 5000;   // 50.00
static constexpr int TICK_MAX     = 15000;  // 150.00
static constexpr int BID_TICK_MIN = 5000;   // 50.00
static constexpr int BID_TICK_MAX = 9999;   // 99.99
static constexpr int ASK_TICK_MIN = 10001;  // 100.01
static constexpr int ASK_TICK_MAX = 15000;  // 150.00
static constexpr int NUM_SLOTS    = TICK_MAX - TICK_MIN + 1; // 10001

inline double tick_to_price(int tick) { return tick * 0.01; }
inline int    price_to_tick(double p) { return static_cast<int>(p * 100.0 + 0.5); }

// ---------------------------------------------------------------------------
// Public API types
// ---------------------------------------------------------------------------

enum class Side : uint8_t { Buy, Sell };

struct Level {
    double price;
    long   qty;
};

struct BBO {
    bool   has_bid   = false;
    double bid_price = 0.0;
    long   bid_qty   = 0;

    bool   has_ask   = false;
    double ask_price = 0.0;
    long   ask_qty   = 0;
};

// Internal per-order record (shared by all implementations)
struct Order {
    uint64_t id;
    Side     side;
    int      tick;  // integer tick representation of price
    long     qty;
};
