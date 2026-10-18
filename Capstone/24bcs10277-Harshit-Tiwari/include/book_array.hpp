#pragma once

#include "common.hpp"

class ArrayBook {
public:
    ArrayBook() : level_qty_(MAX_ASK_TICK - MIN_BID_TICK + 1, 0) { rebuild(1 << 16); }

    bool add_order(uint64_t id, Side side, double price, long qty) {
        int tick = checked_tick(side, price);
        if (tick < 0 || qty <= 0) return false;
        if (find_slot(id) >= 0) return false;

        if ((used_ + 1) * 2 > slots_.size())
            rebuild(live_ * 4 > slots_.size() ? slots_.size() * 2 : slots_.size());
        insert_slot(id, tick, qty);

        level(tick) += qty;
        if (side == Side::Buy && tick > best_bid_) best_bid_ = tick;
        if (side == Side::Sell && (best_ask_ == NONE || tick < best_ask_)) best_ask_ = tick;
        return true;
    }

    bool cancel_order(uint64_t id) {
        long i = find_slot(id);
        if (i < 0) return false;

        int tick = slots_[i].tick;
        level(tick) -= slots_[i].qty;
        slots_[i].tick = TOMBSTONE;
        live_--;

        if (level(tick) == 0) {
            if (tick == best_bid_) best_bid_ = next_bid(tick - 1);
            if (tick == best_ask_) best_ask_ = next_ask(tick + 1);
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b{false, 0.0, 0, false, 0.0, 0};
        if (best_bid_ != NONE) {
            b.has_bid = true;
            b.bid_price = to_price(best_bid_);
            b.bid_qty = level(best_bid_);
        }
        if (best_ask_ != NONE) {
            b.has_ask = true;
            b.ask_price = to_price(best_ask_);
            b.ask_qty = level(best_ask_);
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (int t = best_bid_; t != NONE && (int)out.size() < depth; t = next_bid(t - 1))
            out.push_back({to_price(t), level(t)});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (int t = best_ask_; t != NONE && (int)out.size() < depth; t = next_ask(t + 1))
            out.push_back({to_price(t), level(t)});
        return out;
    }

private:
    static constexpr int NONE = 0;
    static constexpr int EMPTY = 0;
    static constexpr int TOMBSTONE = -1;

    struct Slot {
        uint64_t id;
        long qty;
        int tick;  // EMPTY, TOMBSTONE, or the order's price tick
    };

    long& level(int tick) { return level_qty_[tick - MIN_BID_TICK]; }
    long level(int tick) const { return level_qty_[tick - MIN_BID_TICK]; }

    int next_bid(int from) const {
        for (int t = from; t >= MIN_BID_TICK; t--)
            if (level(t) > 0) return t;
        return NONE;
    }

    int next_ask(int from) const {
        for (int t = from; t <= MAX_ASK_TICK; t++)
            if (level(t) > 0) return t;
        return NONE;
    }

    size_t home(uint64_t id) const { return (id * 0x9E3779B97F4A7C15ULL) >> shift_; }

    long find_slot(uint64_t id) const {
        for (size_t i = home(id);; i = (i + 1) & mask_) {
            if (slots_[i].tick == EMPTY) return -1;
            if (slots_[i].tick != TOMBSTONE && slots_[i].id == id) return (long)i;
        }
    }

    void insert_slot(uint64_t id, int tick, long qty) {
        size_t i = home(id);
        while (slots_[i].tick > 0) i = (i + 1) & mask_;
        if (slots_[i].tick == EMPTY) used_++;
        slots_[i] = Slot{id, qty, tick};
        live_++;
    }

    // Only live orders are copied, so this also drops tombstones.
    void rebuild(size_t capacity) {
        std::vector<Slot> old;
        old.swap(slots_);
        slots_.assign(capacity, Slot{0, 0, EMPTY});
        mask_ = capacity - 1;
        shift_ = 64 - __builtin_ctzll(capacity);
        used_ = 0;
        live_ = 0;
        for (const Slot& s : old)
            if (s.tick > 0) insert_slot(s.id, s.tick, s.qty);
    }

    std::vector<long> level_qty_;
    int best_bid_ = NONE;
    int best_ask_ = NONE;

    std::vector<Slot> slots_;
    size_t mask_ = 0;
    int shift_ = 0;
    size_t used_ = 0;  // live + tombstones
    size_t live_ = 0;
};
