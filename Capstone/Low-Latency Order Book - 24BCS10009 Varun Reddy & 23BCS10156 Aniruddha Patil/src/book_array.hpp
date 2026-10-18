// Implementation C (own design): price-indexed flat array + occupancy bitmap +
// cached best prices + open-addressing order table.
//
//   price levels : int64 total quantity per tick in one fixed array of 10,001 entries
//                  (index = tick - 5000); a level is "present" when its qty > 0
//   occupancy    : TickBitmap with one bit per tick, to skip empty levels in O(1)
//   best price   : best_bid_ / best_ask_ indices cached and updated on add/cancel;
//                  only cancelling the last order at the best price triggers a bitmap
//                  search for the next best level
//   orders       : OrderTable (linear-probing hash table of 16-byte slots, pre-sized)
//   cancel       : one hash probe -> slot holds tick and qty -> direct array index
#pragma once

#include <array>

#include "common.hpp"
#include "order_table.hpp"
#include "tick_bitmap.hpp"

class OrderBookArray {
public:
    static constexpr const char* kName = "C: tick-indexed array + bitmap + open-addressing order table";

    explicit OrderBookArray(size_t expected_orders = size_t{1} << 19) : orders_(expected_orders) {}

    bool add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;
    size_t order_count() const { return orders_.size(); }

private:
    static constexpr int kNone = -1;

    static double index_price(int idx) { return px::to_price(idx + px::kMinTick); }

    std::array<int64_t, px::kNumTicks> level_qty_{};
    TickBitmap occupied_;
    int best_bid_ = kNone;  // tick index of best bid, or kNone
    int best_ask_ = kNone;  // tick index of best ask, or kNone
    OrderTable orders_;
};
