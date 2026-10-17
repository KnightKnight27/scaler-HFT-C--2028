// Implementation C: our own design, everything indexed by price tick.
//
//   price levels : one array of 15001 quantities, indexed directly by tick
//                  (bids live in 5000..9999 and asks in 10001..15000, so one
//                  array is enough for both sides)
//   orders       : open addressing hash table (linear probing) that stores the
//                  whole order inside the table slot, no separate nodes
//   best price   : cached best_bid_ / best_ask_. They are only searched for
//                  when the best level becomes empty, using a 2 level bitmap
//                  of non-empty ticks and ctz/clz instructions
//   cancel       : hash lookup of the id gives tick + qty, then the quantity
//                  array is updated directly
//
// Uses GCC/Clang builtins for the bit scans (C++17 has no std::countl_zero).
#pragma once

#include <algorithm>
#include <cstddef>

#include "types.hpp"

// Bitmap of which ticks have a non-empty level. A second "summary" bitmap
// remembers which words of the first one are non-zero, so finding the next
// set bit never walks more than a few words.
class TickBitmap {
public:
    void set(int t) {
        words_[t >> 6] |= 1ull << (t & 63);
        summary_[t >> 12] |= 1ull << ((t >> 6) & 63);
    }

    void clear(int t) {
        int w = t >> 6;
        words_[w] &= ~(1ull << (t & 63));
        if (words_[w] == 0) summary_[w >> 6] &= ~(1ull << (w & 63));
    }

    // smallest set tick >= t, or -1
    int next_at_or_above(int t) const {
        int w = t >> 6;
        uint64_t m = words_[w] & (~0ull << (t & 63));
        if (m) return (w << 6) + __builtin_ctzll(m);

        int sw = w >> 6;
        int b = (w & 63) + 1;
        uint64_t sm = (b == 64) ? 0 : summary_[sw] & (~0ull << b);
        while (true) {
            if (sm) {
                int nw = (sw << 6) + __builtin_ctzll(sm);
                return (nw << 6) + __builtin_ctzll(words_[nw]);
            }
            if (++sw >= kSummaryWords) return -1;
            sm = summary_[sw];
        }
    }

    // largest set tick <= t, or -1
    int prev_at_or_below(int t) const {
        int w = t >> 6;
        uint64_t m = words_[w] & (~0ull >> (63 - (t & 63)));
        if (m) return (w << 6) + 63 - __builtin_clzll(m);

        int sw = w >> 6;
        uint64_t sm = summary_[sw] & ((1ull << (w & 63)) - 1);
        while (true) {
            if (sm) {
                int nw = (sw << 6) + 63 - __builtin_clzll(sm);
                return (nw << 6) + 63 - __builtin_clzll(words_[nw]);
            }
            if (sw == 0) return -1;
            sm = summary_[--sw];
        }
    }

private:
    static constexpr int kWords = (kNumTicks + 63) / 64;
    static constexpr int kSummaryWords = (kWords + 63) / 64;

    uint64_t words_[kWords] = {};
    uint64_t summary_[kSummaryWords] = {};
};

// id -> order. An empty slot is marked by tick == 0 (no valid order has it).
class OrderTable {
public:
    struct Entry {
        uint64_t id;
        long qty;
        int tick;
        Side side;
    };

    explicit OrderTable(size_t initial_slots = 1u << 20) { init(initial_slots); }

    Entry* find(uint64_t id) {
        size_t i = home(id);
        while (slots_[i].tick != 0) {
            if (slots_[i].id == id) return &slots_[i];
            i = (i + 1) & mask_;
        }
        return nullptr;
    }

    // false if the id is already there
    bool insert(uint64_t id, const Order& o) {
        if ((count_ + 1) * 2 > slots_.size()) grow();

        size_t i = home(id);
        while (slots_[i].tick != 0) {
            if (slots_[i].id == id) return false;
            i = (i + 1) & mask_;
        }
        slots_[i] = Entry{id, o.qty, o.tick, o.side};
        ++count_;
        return true;
    }

    // Removes the entry and shifts later entries of the same probe run back,
    // so no tombstones are needed.
    void erase(Entry* e) {
        size_t i = e - slots_.data();
        size_t j = i;
        while (true) {
            j = (j + 1) & mask_;
            if (slots_[j].tick == 0) break;

            size_t k = home(slots_[j].id);
            bool stays = (i <= j) ? (i < k && k <= j) : (i < k || k <= j);
            if (stays) continue;

            slots_[i] = slots_[j];
            i = j;
        }
        slots_[i].tick = 0;
        --count_;
    }

    size_t size() const { return count_; }

private:
    void init(size_t n) {
        slots_.assign(n, Entry{0, 0, 0, Side::Buy});
        mask_ = n - 1;
        shift_ = 64;
        for (size_t s = n; s > 1; s >>= 1) --shift_;
        count_ = 0;
    }

    size_t home(uint64_t id) const {
        return (id * 0x9E3779B97F4A7C15ull) >> shift_;  // fibonacci hashing
    }

    void grow() {
        std::vector<Entry> old;
        old.swap(slots_);
        init(old.size() * 2);
        for (const Entry& e : old)
            if (e.tick != 0) insert(e.id, Order{e.qty, e.tick, e.side});
    }

    std::vector<Entry> slots_;
    size_t mask_ = 0;
    int shift_ = 0;
    size_t count_ = 0;
};

class ArrayBook {
public:
    ArrayBook() : qty_(kNumTicks, 0) {}

    bool add_order(uint64_t id, Side side, int tick, long qty) {
        if (qty <= 0 || !valid_tick(side, tick)) return false;
        if (!orders_.insert(id, Order{qty, tick, side})) return false;

        if (qty_[tick] == 0) ticks_.set(tick);
        qty_[tick] += qty;

        if (side == Side::Buy) {
            if (tick > best_bid_) best_bid_ = tick;
        } else {
            if (tick < best_ask_) best_ask_ = tick;
        }
        return true;
    }

    bool cancel_order(uint64_t id) {
        OrderTable::Entry* e = orders_.find(id);
        if (!e) return false;

        int tick = e->tick;
        long qty = e->qty;
        Side side = e->side;
        orders_.erase(e);

        qty_[tick] -= qty;
        if (qty_[tick] == 0) {
            ticks_.clear(tick);
            // the best level just disappeared, look for the next one
            if (side == Side::Buy && tick == best_bid_) {
                best_bid_ = ticks_.prev_at_or_below(tick);
            } else if (side == Side::Sell && tick == best_ask_) {
                int next = ticks_.next_at_or_above(tick);
                best_ask_ = (next < 0) ? kNoAsk : next;
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (best_bid_ != kNoBid) {
            b.has_bid = true;
            b.bid_price = best_bid_;
            b.bid_qty = qty_[best_bid_];
        }
        if (best_ask_ != kNoAsk) {
            b.has_ask = true;
            b.ask_price = best_ask_;
            b.ask_qty = qty_[best_ask_];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        out.reserve(std::max(0, std::min(depth, 32)));
        int t = best_bid_;
        while (t != kNoBid && (int)out.size() < depth) {
            out.push_back({t, qty_[t]});
            t = ticks_.prev_at_or_below(t - 1);
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        out.reserve(std::max(0, std::min(depth, 32)));
        int t = best_ask_;
        while (t != kNoAsk && (int)out.size() < depth) {
            out.push_back({t, qty_[t]});
            int next = ticks_.next_at_or_above(t + 1);
            t = (next < 0) ? kNoAsk : next;
        }
        return out;
    }

    size_t live_orders() const { return orders_.size(); }

private:
    // "no best price" markers, chosen so that the comparisons in add_order work
    static constexpr int kNoBid = -1;
    static constexpr int kNoAsk = kNumTicks;

    std::vector<long> qty_;
    TickBitmap ticks_;
    OrderTable orders_;
    int best_bid_ = kNoBid;
    int best_ask_ = kNoAsk;
};
