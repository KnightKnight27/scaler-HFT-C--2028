#pragma once

#include "types.hpp"
#include <cstdint>
#include <vector>

class OrderBookB {
public:
    OrderBookB();
    ~OrderBookB() = default;

    void add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

private:
    struct TickLevel {
        int32_t tick;
        long qty;
    };

    enum SlotState : uint8_t { Empty = 0, Occupied = 1, Tombstone = 2 };

    struct HashSlot {
        uint64_t id = 0;
        int32_t tick = 0;
        long qty = 0;
        Side side = Side::Buy;
        SlotState state = SlotState::Empty;
    };

    static constexpr size_t HASH_CAPACITY = 1ULL << 21; // 2,097,152 slots (load factor < 0.35)
    static constexpr size_t HASH_MASK = HASH_CAPACITY - 1;

    // bids_ sorted ascending by tick (5000 -> 9999) so Best Bid is at .back()
    // asks_ sorted descending by tick (15000 -> 10001) so Best Ask is at .back()
    std::vector<TickLevel> bids_;
    std::vector<TickLevel> asks_;
    std::vector<HashSlot> order_table_;

    static inline size_t hash_id(uint64_t x) noexcept {
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        x = x ^ (x >> 31);
        return static_cast<size_t>(x) & HASH_MASK;
    }

    void insert_order_slot(uint64_t id, Side side, int32_t tick, long qty) noexcept;
    HashSlot* find_order_slot(uint64_t id) noexcept;
};