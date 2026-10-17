#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Common.h"

// Implementation C: my own design, built around the fact that the whole
// instrument only ever has ~10,000 valid price levels.
//
// - Per-price storage: a single flat std::array<long, kArraySize>, indexed
//   directly by (ticks - kMinTick). No hashing, no tree, no pointers -- just
//   one contiguous block of memory. Bid ticks and ask ticks live in the same
//   array at different index ranges (the mid-price slot in between is never
//   touched).
// - Per-order storage: a std::vector<OrderSlot> indexed directly by order id
//   (grown with manual doubling so repeated growth stays amortized O(1)).
//   This assumes ids are reasonably dense/small, which holds for the
//   benchmark's sequential id generator -- see README for the trade-off.
// - Best bid/ask: two plain integers, bestBidIndex_/bestAskIndex_, updated
//   in O(1) whenever a *new* level opens at a better price. They are only
//   recomputed by scanning when the level that WAS best becomes empty --
//   everything else is a single array write.
// - Cancellation: order id directly indexes the order table, O(1), no scan.
//
// Complexity:
//   add_order    O(1) amortized (array write + O(1) best-pointer check;
//                occasional order-table growth is amortized O(1) via doubling)
//   cancel_order O(1) typical; O(k) worst case only when the level removed
//                was the current best, where k = number of consecutive empty
//                ticks between the old best and the next occupied one
//   get_bbo      O(1): just reads the cached best index
//   get_bids/ks  O(depth) typical, O(ticks scanned) worst case on a sparse
//                book -- but the scan is a straight-line walk over
//                contiguous memory, so it stays fast even when it has to
//                look at more than `depth` slots
class OrderBookC
{
public:
    void add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

private:
    static constexpr int kMinTick = 5000;     // 50.00
    static constexpr int kMaxBidTick = 9999;  // 99.99
    static constexpr int kMinAskTick = 10001; // 100.01
    static constexpr int kMaxTick = 15000;    // 150.00
    static constexpr int kArraySize = kMaxTick - kMinTick + 1;

    static constexpr int kBidLowIndex = 0;
    static constexpr int kBidHighIndex = kMaxBidTick - kMinTick;
    static constexpr int kAskLowIndex = kMinAskTick - kMinTick;
    static constexpr int kAskHighIndex = kMaxTick - kMinTick;

    static int indexOf(int ticks) { return ticks - kMinTick; }

    struct OrderSlot
    {
        bool active = false;
        Side side = Side::Buy;
        int tickIndex = -1;
        long qty = 0;
    };

    std::array<long, kArraySize> levelQty_{};
    std::vector<OrderSlot> orders_;

    int bestBidIndex_ = -1;
    int bestAskIndex_ = -1;

    void ensureOrderCapacity(uint64_t id);
    void onLevelFilled(Side side, int idx);
    void onLevelEmptied(Side side, int idx);
};
