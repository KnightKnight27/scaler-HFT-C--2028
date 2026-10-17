#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "common.hpp"
#include "order_table.hpp"

// C: quantities indexed by price tick, with a hash table for orders.
// A bitmap finds the next occupied level when the best level becomes empty.
class ArrayOrderBook {
public:
    explicit ArrayOrderBook(std::size_t expected_orders = 0);

    bool add_order(std::uint64_t id, Side side, double price, long qty);
    bool cancel_order(std::uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

private:
    // Index 0 is 50.00 and index 10000 is 150.00. Bids use indexes 0 to 4999,
    // asks use 5001 to 10000, and index 5000 (the mid, 100.00) stays empty.
    static constexpr int kNumLevels = kMaxAskTick - kMinBidTick + 1;
    static constexpr int kBitmapWords = (kNumLevels + 63) / 64;
    static constexpr int kNoBid = -1;          // best_bid_ when there are no bids
    static constexpr int kNoAsk = kNumLevels;  // best_ask_ when there are no asks

    static int index_of(Tick tick) { return tick - kMinBidTick; }
    static double price_at(int index) { return tick_to_price(kMinBidTick + index); }

    void mark_non_empty(int index);
    void mark_empty(int index);
    int highest_non_empty_at_or_below(int index) const;
    int lowest_non_empty_at_or_above(int index) const;

    std::vector<long> level_qty_;                          // total quantity at each index
    std::array<std::uint64_t, kBitmapWords> non_empty_{};  // bit i is set when level_qty_[i] > 0
    int best_bid_ = kNoBid;
    int best_ask_ = kNoAsk;
    OrderTable orders_;
};
