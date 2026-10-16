#pragma once

#include "Types.hpp"
#include <vector>
#include <cstring>

class OrderBookC : public OrderBook {
public:
    static constexpr size_t NUM_TICKS = 10001; // [50.00, 150.00] at 0.01 tick size
    static constexpr size_t NUM_WORDS = (NUM_TICKS + 63) / 64; // 157 64-bit words

    explicit OrderBookC(size_t initial_order_capacity = 2097152); // 2^21 preallocated slots
    ~OrderBookC() override;

    // Non-copyable for performance safety
    OrderBookC(const OrderBookC&) = delete;
    OrderBookC& operator=(const OrderBookC&) = delete;

    void add_order(uint64_t id, Side side, double price, long qty) override;
    bool cancel_order(uint64_t id) override;
    BBO get_bbo() const override;
    std::vector<Level> get_bids(int depth = 10) const override;
    std::vector<Level> get_asks(int depth = 10) const override;

private:
    struct alignas(32) OrderSlot {
        uint64_t id;      // Order ID
        long qty;         // Quantity
        uint16_t tick;    // Price tick index [0, 10000]
        uint8_t side;     // 0 = Buy, 1 = Sell
        bool occupied;    // Slot status
    };

    inline size_t hash_id(uint64_t x) const {
        // Fast 64-bit SplitMix-like integer hash
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return static_cast<size_t>(x);
    }

    void rehash(size_t new_capacity);

    inline void set_active(int tick) {
        active_mask_[tick / 64] |= (1ULL << (tick % 64));
    }

    inline void clear_active(int tick) {
        active_mask_[tick / 64] &= ~(1ULL << (tick % 64));
    }

    int find_prev_active_bid(int from_tick) const;
    int find_next_active_ask(int from_tick) const;

    // Price level quantities indexed directly by tick [0 .. 10000]
    long price_qty_[NUM_TICKS];

    // 64-bit word bitmap for instant O(1) detection of non-empty levels
    uint64_t active_mask_[NUM_WORDS];

    // Cached best bid and ask ticks for instant O(1) BBO retrieval
    int best_bid_tick_;
    int best_ask_tick_;

    // Preallocated flat open-addressing hash table for zero-allocation order storage
    OrderSlot* order_table_;
    size_t capacity_;
    size_t mask_;
    size_t order_count_;
};
