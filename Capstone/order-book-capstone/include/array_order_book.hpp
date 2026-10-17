#pragma once
// =============================================================================
// array_order_book.hpp  –  Implementation C: Fixed-size price-indexed array
// =============================================================================
// Design
// ------
// We have exactly 10,000 valid price ticks split into:
//   Bid slots : ticks 5000..9999   → array index = tick - TICK_BID_MIN   (0..4999)
//   Ask slots : ticks 10001..15000 → array index = tick - TICK_ASK_MIN   (0..4999)
//
// Each side has a fixed-size array of Slot:
//   struct Slot { long qty; int order_count; };
//
// Index mapping (no out-of-range access):
//   bid index = tick - 5000   (range 0..4999)
//   ask index = tick - 10001  (range 0..4999)
//
// Best-price tracking
// -------------------
// Instead of scanning all 5000 slots on every BBO query we maintain:
//   best_bid_tick_ : highest occupied bid tick, or -1 if none
//   best_ask_tick_ : lowest  occupied ask tick, or INT_MAX if none
//
// After a cancellation empties the current best slot, we scan DOWNWARD (bid)
// or UPWARD (ask) from the previous best.  In the common case this scan is
// very short (O(1) amortized for random workloads); worst case is O(N) where
// N is the number of price levels (5000 per side).
//
// Order-ID lookup
// ---------------
//   order_map_ : std::unordered_map<uint64_t, OrderMeta>
//   OrderMeta  : { side, tick, qty }
//
// Operation complexities
// ----------------------
//   add_order    : O(1)       — direct array index, O(1) best-pointer update
//   cancel_order : O(1) avg   — hash lookup + direct array update
//                  O(N) worst — when best level is removed and rescan required
//                  (N = 5000 per side, the number of price slots)
//   get_bbo      : O(1)       — read best_bid_tick_ / best_ask_tick_
//   get_bids(d)  : O(S)       — walks DOWN from best_bid_tick_, visiting every
//                  slot (occupied or empty) until d occupied levels are found.
//                  S is the number of slots examined; S == d only when the d
//                  highest occupied levels are contiguous.  In the worst case
//                  (d occupied levels spread across all 5000 slots), S = 5000.
//   get_asks(d)  : O(S)       — symmetric, walks UP from best_ask_tick_
// =============================================================================

#include "order_book.hpp"
#include <unordered_map>
#include <climits>

namespace hft {

class ArrayOrderBook : public IOrderBook {
public:
    ArrayOrderBook();

    void add_order(std::uint64_t id, Side side, double price, long qty) override;
    bool cancel_order(std::uint64_t id) override;
    BBO  get_bbo() const override;
    std::vector<Level> get_bids(int depth = 10) const override;
    std::vector<Level> get_asks(int depth = 10) const override;

private:
    static constexpr int BID_SLOTS = TICK_BID_MAX - TICK_BID_MIN + 1; // 5000
    static constexpr int ASK_SLOTS = TICK_ASK_MAX - TICK_ASK_MIN + 1; // 5000

    struct Slot {
        long qty         = 0;
        int  order_count = 0;
    };

    struct OrderMeta {
        Side side;
        int  tick;
        long qty;
    };

    Slot bid_slots_[BID_SLOTS];
    Slot ask_slots_[ASK_SLOTS];

    int best_bid_tick_;  // -1 if no bids
    int best_ask_tick_;  // INT_MAX if no asks

    std::unordered_map<std::uint64_t, OrderMeta> order_map_;

    // Map tick to slot array index (no bounds check – caller must validate tick)
    static int bid_index(int tick) noexcept { return tick - TICK_BID_MIN; }
    static int ask_index(int tick) noexcept { return tick - TICK_ASK_MIN; }

    // Rescan for new best after level removal
    void rescan_best_bid() noexcept;
    void rescan_best_ask() noexcept;
};

} // namespace hft
