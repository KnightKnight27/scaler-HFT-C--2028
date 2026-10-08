#pragma once

#include "OrderBookBase.hpp"
#include <unordered_map>
#include <vector>
#include <cstring>
#include <cstdint>

/**
 * @brief Implementation C: Direct Price-Indexed Flat Array (Tick Table + Bitmap & Cached BBO)
 * 
 * Custom High-Frequency Trading (HFT) Architecture:
 * - Direct Tick Indexing: Exploits the fixed 0.01 tick size over [50.00, 150.00] (10,001 levels).
 *   Level quantities are stored in a contiguous, cache-aligned flat array (10,001 entries = ~80 KB).
 * - Hierarchical 64-bit Bitmaps: Active levels are tracked across 157 64-bit words (~1.2 KB),
 *   residing permanently in the CPU L1 data cache.
 * - Hardware Intrinsics: Uses __builtin_clzll (Count Leading Zeros) and __builtin_ctzll (Count Trailing Zeros)
 *   to locate adjacent active price levels in ~1-3 CPU cycles.
 * - Cached BBO: Maintains best_bid_tick_ and best_ask_tick_ for instant O(1) BBO queries.
 * - Zero Heap Allocations: Price level operations require no dynamic memory allocations or tree rebalancing.
 */
class OrderBookC : public OrderBookBase {
public:
    static constexpr int NUM_WORDS = (Config::NUM_TICKS + 63) / 64;

    OrderBookC();
    ~OrderBookC() override = default;

    void add_order(uint64_t id, Side side, double price, long qty) override;
    bool cancel_order(uint64_t id) override;
    BBO get_bbo() const override;
    std::vector<Level> get_bids(int depth = 10) const override;
    std::vector<Level> get_asks(int depth = 10) const override;

    std::string get_name() const override { return "Implementation C (Direct Price-Indexed Flat Array + Bitmap)"; }

    size_t bid_level_count() const { return bid_level_count_; }
    size_t ask_level_count() const { return ask_level_count_; }
    size_t live_order_count() const { return orders_.size(); }

private:
    int find_prev_active_bid(int from_tick) const;
    int find_next_active_ask(int from_tick) const;

    long quantities_[Config::NUM_TICKS];
    uint64_t bitmap_[NUM_WORDS];

    int best_bid_tick_{-1};
    int best_ask_tick_{-1};
    size_t bid_level_count_{0};
    size_t ask_level_count_{0};

    std::unordered_map<uint64_t, OrderRecord> orders_;
};
