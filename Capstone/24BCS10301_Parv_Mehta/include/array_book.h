#pragma once
// Implementation C: flat arrays, no per-order allocation.
//
// Price levels: one int32 quantity per possible tick, 5000 per side. The
// index comes straight from the tick, so there is nothing to search.
//   bids: index = tick - 5000   (50.00 -> 0,  99.99 -> 4999)
//   asks: index = 15000 - tick  (150.00 -> 0, 100.01 -> 4999)
// With this, a bigger index is always a better price on both sides and the
// two sides share the same code.
//
// Best price: a two-level bitmap over the level array. occ has one bit per
// level, summary has one bit per occ word. The highest set bit is found with
// clz, so finding the next best level is at most three word reads. The best
// index is also cached, so get_bbo is just two loads.
//
// Orders: a hand written open-addressing hash table (linear probing). Slots
// are 16 bytes {id, qty, tick}. Deleting shifts later entries back instead
// of leaving tombstones. The table is sized up front so the hot path never
// calls malloc.

#include <vector>

#include "order_book.h"

class ArrayBook {
public:
    static const char* name() { return "flat arrays + two-level bitmap + open-addressing hash"; }

    // expected_orders = live orders we want to hold without rehashing.
    explicit ArrayBook(size_t expected_orders = 1u << 19);

    void add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

    size_t order_count() const { return orders_.size(); }
    size_t order_table_capacity() const { return orders_.capacity(); }

private:
    static Tick bid_tick(int index) { return kMinBidTick + index; }
    static Tick ask_tick(int index) { return kMaxAskTick - index; }

    // Price levels of one side.
    struct SideLevels {
        static const int kWords = (kLevelsPerSide + 63) / 64;   // 79
        static const int kSummaryWords = (kWords + 63) / 64;    // 2

        int32_t qty[kLevelsPerSide];
        uint64_t occ[kWords];
        uint64_t summary[kSummaryWords];
        int best;  // highest occupied index, -1 when empty

        SideLevels();
        void add(int i, int32_t q);
        void remove(int i, int32_t q);
        int highest_below(int i) const;
    };

    // id -> (tick, qty) lookup.
    class OrderTable {
    public:
        struct Slot {
            uint64_t id;
            int32_t qty;
            int32_t tick;
        };
        static const uint64_t kEmpty = ~0ull;  // reserved id meaning "free slot"

        explicit OrderTable(size_t expected);
        bool insert(uint64_t id, int32_t tick, int32_t qty);
        bool erase(uint64_t id, int32_t& tick, int32_t& qty);

        size_t size() const { return size_; }
        size_t capacity() const { return slots_.size(); }

    private:
        void init(size_t cap);
        size_t home(uint64_t id) const;
        void grow();

        std::vector<Slot> slots_;
        size_t mask_ = 0;
        unsigned shift_ = 64;
        size_t size_ = 0;
    };

    SideLevels bids_;
    SideLevels asks_;
    OrderTable orders_;
};
