#pragma once
#include "orderbook.h"
#include <array>
#include <vector>

// Implementation C (own design):
//   price levels : fixed array indexed by tick, one slot per price
//   best price   : integer cursors, rescanned over contiguous memory when the best level empties
//   orders       : flat open-addressing hash table (linear probing, backward-shift delete)
class OrderBookC {
    static constexpr int MIN_TICK = MIN_BID_TICK;
    static constexpr int ARR_SIZE = MAX_ASK_TICK - MIN_BID_TICK + 1;   // 10001 slots
    static constexpr int EMPTY    = -1;                                // marks a free table slot
    static constexpr size_t NPOS  = static_cast<size_t>(-1);

    struct Slot { uint64_t id; long qty; int idx; Side side; };        // 24 bytes

    std::array<long, ARR_SIZE> qty_{};
    std::vector<Slot>          table_;
    size_t                     mask_  = 0;   // capacity - 1 (capacity is a power of two)
    int                        shift_ = 0;   // 64 - log2(capacity)
    size_t                     live_  = 0;
    int                        best_bid_ = -1;
    int                        best_ask_ = ARR_SIZE;

    void init_table(size_t cap) {
        table_.assign(cap, Slot{0, 0, EMPTY, Side::Buy});
        mask_ = cap - 1;
        int lg = 0;
        while ((size_t(1) << lg) < cap) ++lg;
        shift_ = 64 - lg;
    }

    // Fibonacci hashing: spreads sequential ids evenly over the table.
    size_t home(uint64_t id) const {
        return static_cast<size_t>((id * 0x9E3779B97F4A7C15ULL) >> shift_);
    }

    size_t find(uint64_t id) const {
        size_t i = home(id);
        while (table_[i].idx != EMPTY) {
            if (table_[i].id == id) return i;
            i = (i + 1) & mask_;
        }
        return NPOS;
    }

    void grow() {
        std::vector<Slot> old = std::move(table_);
        init_table(old.size() * 2);
        for (const Slot& s : old) {
            if (s.idx == EMPTY) continue;
            size_t i = home(s.id);
            while (table_[i].idx != EMPTY) i = (i + 1) & mask_;
            table_[i] = s;
        }
    }

    void insert(uint64_t id, Side side, int idx, long qty) {
        if ((live_ + 1) * 2 > table_.size()) grow();    // keep load factor <= 0.5
        size_t i = home(id);
        while (table_[i].idx != EMPTY) i = (i + 1) & mask_;
        table_[i] = Slot{id, qty, idx, side};
        ++live_;
    }

    // Backward-shift deletion: no tombstones, so probe chains never degrade over time.
    void erase_at(size_t i) {
        size_t j = i;
        for (;;) {
            j = (j + 1) & mask_;
            if (table_[j].idx == EMPTY) break;
            size_t k = home(table_[j].id);
            bool movable = (i <= j) ? !(i < k && k <= j) : !(i < k || k <= j);
            if (!movable) continue;
            table_[i] = table_[j];
            i = j;
        }
        table_[i].idx = EMPTY;
        --live_;
    }

    static double from_idx(int i) { return tick_to_price(i + MIN_TICK); }

public:
    OrderBookC() { init_table(size_t(1) << 16); }

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = price_to_tick(price);
        assert(in_band(side, tick));
        int idx = tick - MIN_TICK;
        insert(id, side, idx, qty);
        qty_[idx] += qty;
        if (side == Side::Buy) {
            if (idx > best_bid_) best_bid_ = idx;
        } else {
            if (idx < best_ask_) best_ask_ = idx;
        }
    }

    bool cancel_order(uint64_t id) {
        size_t pos = find(id);
        if (pos == NPOS) return false;
        const Slot s = table_[pos];
        erase_at(pos);
        qty_[s.idx] -= s.qty;
        if (qty_[s.idx] == 0) {
            if (s.side == Side::Buy && s.idx == best_bid_)
                while (best_bid_ >= 0 && qty_[best_bid_] == 0) --best_bid_;
            else if (s.side == Side::Sell && s.idx == best_ask_)
                while (best_ask_ < ARR_SIZE && qty_[best_ask_] == 0) ++best_ask_;
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (best_bid_ >= 0) {
            b.has_bid = true; b.bid_price = from_idx(best_bid_); b.bid_qty = qty_[best_bid_];
        }
        if (best_ask_ < ARR_SIZE) {
            b.has_ask = true; b.ask_price = from_idx(best_ask_); b.ask_qty = qty_[best_ask_];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (int i = best_bid_; i >= 0 && (int)out.size() < depth; --i)
            if (qty_[i] > 0) out.push_back({from_idx(i), qty_[i]});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (int i = best_ask_; i < ARR_SIZE && (int)out.size() < depth; ++i)
            if (qty_[i] > 0) out.push_back({from_idx(i), qty_[i]});
        return out;
    }
};
