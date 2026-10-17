// Implementation C (own design): price-indexed flat arrays + occupancy bitmap
// + custom open-addressing hash table for orders. No per-operation heap
// allocation after construction.
//
//  * Price levels : one fixed `long qty[5000]` array per side, indexed by
//                   "rank" (0 = best possible price). Bids: rank = 9999-tick,
//                   asks: rank = tick-10001. Both sides therefore search in the
//                   same direction (lowest set rank = best price).
//  * Best price   : cached `best_` rank per side, plus a 5000-bit occupancy
//                   bitmap (79 x uint64). When the best level empties, the next
//                   best is found with ctz over the bitmap: at most 79 word
//                   loads, usually 1.
//  * Orders       : custom open-addressing hash table (linear probing, Fibonacci
//                   hash, backward-shift deletion so there are no tombstones).
//                   Each slot is 16 bytes {id, tick, qty}; the side is derived
//                   from the tick (tick < 10000 => bid) so it needs no storage.
//                   id ~0 is the empty-slot marker, so that one id is kept in a
//                   dedicated spare slot outside the table.
//  * Cancel       : probe the table for id, read tick, subtract from qty[rank],
//                   clear the bit if the level became empty, and if that was the
//                   best, scan the bitmap forward for the new best.
//
//  add_order   : O(1) avg (probe + array write + bit set)
//  cancel_order: O(1) avg (probe + array write; worst case O(5000/64) bitmap scan)
//  get_bbo     : O(1) (two array reads)
//  get_bids/asks(d): O(d + words scanned) via ctz over the bitmap
#pragma once

#include <cstring>
#include <memory>

#include "order_book_common.hpp"

class OrderBookC {
public:
    explicit OrderBookC(size_t expected_live_orders = 1 << 20) {
        bids_.clear();
        asks_.clear();
        size_t cap = 16;
        while (cap < expected_live_orders * 2) cap <<= 1;  // keep load factor <= 0.5
        table_init(cap);
    }

    // Returns false (and changes nothing) for an out-of-band, off-tick or
    // non-finite price, qty <= 0, or an id that is already live. The band
    // check is what keeps rank() inside [0, 5000): without it a buy at 120.00
    // would index qty[-2001].
    bool add_order(uint64_t id, Side side, double price, long qty) {
        int tick;
        return price_to_tick(price, tick) && add_order_ticks(id, side, tick, qty);
    }

    bool add_order_ticks(uint64_t id, Side side, int tick, long qty) {
        if (!valid_order(side, tick, qty)) return false;
        const bool inserted = id == kEmpty ? spare_insert(tick, static_cast<int32_t>(qty))
                                           : table_insert(id, tick, static_cast<int32_t>(qty));
        if (!inserted) return false;  // duplicate id
        SideBook& sb = side == Side::Buy ? bids_ : asks_;
        const int r = rank(side, tick);
        if (sb.qty[r] == 0) {
            sb.bits[r >> 6] |= (1ull << (r & 63));
            if (r < sb.best) sb.best = r;
        }
        sb.qty[r] += qty;
        return true;
    }

    bool cancel_order(uint64_t id) {
        int tick = 0;
        int32_t qty = 0;
        if (!table_erase(id, tick, qty)) return false;
        const Side side = tick < 10000 ? Side::Buy : Side::Sell;
        SideBook& sb = side == Side::Buy ? bids_ : asks_;
        const int r = rank(side, tick);
        sb.qty[r] -= qty;
        if (sb.qty[r] == 0) {
            sb.bits[r >> 6] &= ~(1ull << (r & 63));
            if (r == sb.best) sb.best = sb.next_set(r);
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
        if (bids_.best != kNone) {
            b.has_bid = true;
            b.bid_price = to_price(unrank(Side::Buy, bids_.best));
            b.bid_qty = bids_.qty[bids_.best];
        }
        if (asks_.best != kNone) {
            b.has_ask = true;
            b.ask_price = to_price(unrank(Side::Sell, asks_.best));
            b.ask_qty = asks_.qty[asks_.best];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top(Side::Buy, bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top(Side::Sell, asks_, depth); }

private:
    static constexpr int kNone = kLevelsPerSide;  // "no level" sentinel for best_
    static constexpr int kWords = (kLevelsPerSide + 63) / 64;

    // ---- price side ------------------------------------------------------
    struct SideBook {
        long qty[kLevelsPerSide];
        uint64_t bits[kWords];
        int best;

        void clear() {
            std::memset(qty, 0, sizeof(qty));
            std::memset(bits, 0, sizeof(bits));
            best = kNone;
        }
        // first set bit with rank > r, or kNone
        int next_set(int r) const {
            int w = (r + 1) >> 6;
            if (w >= kWords) return kNone;
            uint64_t word = bits[w] & (~0ull << ((r + 1) & 63));
            while (true) {
                if (word) return (w << 6) + __builtin_ctzll(word);
                if (++w >= kWords) return kNone;
                word = bits[w];
            }
        }
    };

    static int rank(Side s, int tick) { return s == Side::Buy ? kBidMaxTick - tick : tick - kAskMinTick; }
    static int unrank(Side s, int r) { return s == Side::Buy ? kBidMaxTick - r : kAskMinTick + r; }

    static std::vector<Level> top(Side s, const SideBook& sb, int depth) {
        std::vector<Level> out;
        out.reserve(depth_capacity(depth));
        for (int r = sb.best; r != kNone && depth > 0; r = sb.next_set(r), --depth)
            out.push_back(Level{to_price(unrank(s, r)), sb.qty[r]});
        return out;
    }

    // ---- order table: open addressing, linear probing ---------------------
    struct Slot {
        uint64_t id;   // kEmpty if unused
        int32_t tick;
        int32_t qty;
    };
    static constexpr uint64_t kEmpty = ~0ull;

    static size_t hash(uint64_t id) { return static_cast<size_t>(id * 0x9E3779B97F4A7C15ull); }
    size_t index_of(uint64_t id) const { return hash(id) >> shift_; }

    void table_init(size_t cap) {
        cap_ = cap;
        mask_ = cap - 1;
        shift_ = 64 - __builtin_ctzll(cap);
        size_ = 0;
        slots_.reset(new Slot[cap + 1]);  // +1: the spare slot, see spare()
        for (size_t i = 0; i <= cap; ++i) slots_[i].id = kEmpty;
        spare().qty = 0;
    }

    void grow() {
        std::unique_ptr<Slot[]> old = std::move(slots_);
        const size_t old_cap = cap_;
        table_init(cap_ * 2);
        for (size_t i = 0; i < old_cap; ++i)
            if (old[i].id != kEmpty) table_insert(old[i].id, old[i].tick, old[i].qty);
        spare() = old[old_cap];
    }

    bool table_insert(uint64_t id, int tick, int32_t qty) {
        if ((size_ + 1) * 2 > cap_) grow();
        size_t i = index_of(id);
        while (slots_[i].id != kEmpty) {
            if (slots_[i].id == id) return false;
            i = (i + 1) & mask_;
        }
        slots_[i] = Slot{id, tick, qty};
        ++size_;
        return true;
    }

    bool table_erase(uint64_t id, int& tick, int32_t& qty) {
        size_t i = index_of(id);
        while (true) {
            if (slots_[i].id == kEmpty) return id == kEmpty && spare_erase(tick, qty);
            if (slots_[i].id == id) break;
            i = (i + 1) & mask_;
        }
        tick = slots_[i].tick;
        qty = slots_[i].qty;
        --size_;
        // backward-shift deletion: pull following cluster members back so that
        // no tombstone is needed and probe chains stay short.
        size_t hole = i;
        size_t j = (i + 1) & mask_;
        while (slots_[j].id != kEmpty) {
            const size_t home = index_of(slots_[j].id);
            // can slot j legally move into `hole`? yes iff home is not in (hole, j]
            const bool wraps = hole > j;
            const bool between = wraps ? (home > hole || home <= j) : (home > hole && home <= j);
            if (!between) {
                slots_[hole] = slots_[j];
                hole = j;
            }
            j = (j + 1) & mask_;
        }
        slots_[hole].id = kEmpty;
        return true;
    }

    // kEmpty doubles as the empty-slot marker, so an order whose id is ~0 cannot
    // live in the table. It gets one spare slot instead, which keeps C accepting
    // the full uint64_t id range like A and B. The spare is slots_[cap_], one
    // past the probed range (indices are masked to < cap_), and qty > 0 marks it
    // live since every live order has qty >= 1. Keeping it in the heap array
    // rather than adding members leaves sizeof(OrderBookC) unchanged: adding
    // three unused members alone measurably shifted impl C's total time.
    // Only the add path's existing id check and the erase miss path touch it.
    Slot& spare() { return slots_[cap_]; }

    bool spare_insert(int tick, int32_t qty) {
        if (spare().qty > 0) return false;
        spare() = Slot{kEmpty, tick, qty};
        return true;
    }

    bool spare_erase(int& tick, int32_t& qty) {
        if (spare().qty <= 0) return false;
        tick = spare().tick;
        qty = spare().qty;
        spare().qty = 0;
        return true;
    }

    SideBook bids_;
    SideBook asks_;
    std::unique_ptr<Slot[]> slots_;
    size_t cap_ = 0, mask_ = 0, size_ = 0;
    unsigned shift_ = 64;
};
