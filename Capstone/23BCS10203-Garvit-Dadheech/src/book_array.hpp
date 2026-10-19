// Implementation C (my own): the price range is fixed at 10,000 ticks, so
// levels are a plain array indexed by tick, the best bid/ask are cached
// indices, and a bitmap (one bit per level + a summary word) finds the next
// non-empty level with clz/ctz when the best one empties.  Orders live in a
// flat open addressing hash table (linear probing, backward shift delete), so
// nothing is allocated per order.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "common.hpp"

class ArrayBook {
public:
    static constexpr const char* name() {
        return "C: fixed price array + two-level bitmap + open-addressing order table";
    }

    explicit ArrayBook(size_t expected_live_orders = 1u << 19) {
        size_t cap = 1024;
        while (cap * kMaxLoadNum < expected_live_orders * kMaxLoadDen) cap <<= 1;
        rehash(cap);
    }

    OB_API void add_order(uint64_t id, Side side, double price, long qty) {
        const int tick = ob::to_tick(price);
        const int slot = tick - kBaseTick;

        insert_order(id, tick, qty);

        const bool was_empty = (qty_[slot] == 0);
        qty_[slot] += qty;
        if (was_empty) {
            bits_.set(slot);
            if (side == Side::Buy) {
                if (slot > best_bid_) best_bid_ = slot;
            } else {
                if (slot < best_ask_) best_ask_ = slot;
            }
        }
    }

    OB_API bool cancel_order(uint64_t id) {
        int tick, qty;
        if (!erase_order(id, tick, qty)) return false;

        const int slot = tick - kBaseTick;
        qty_[slot] -= qty;
        if (qty_[slot] == 0) {
            bits_.clear(slot);
            if (slot == best_bid_) {
                best_bid_ = bits_.prev_set(slot);  // -1 if no bids left
            } else if (slot == best_ask_) {
                best_ask_ = bits_.next_set(slot);  // kSlots if no asks left
            }
        }
        return true;
    }

    OB_API BBO get_bbo() const {
        BBO out = ob::empty_bbo();
        if (best_bid_ >= 0) {
            out.has_bid = true;
            out.bid_price = ob::to_price(best_bid_ + kBaseTick);
            out.bid_qty = qty_[best_bid_];
        }
        if (best_ask_ < kSlots) {
            out.has_ask = true;
            out.ask_price = ob::to_price(best_ask_ + kBaseTick);
            out.ask_qty = qty_[best_ask_];
        }
        return out;
    }

    OB_API std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(static_cast<size_t>(depth));
        for (int s = best_bid_; s >= 0 && depth > 0; s = bits_.prev_set(s), --depth) {
            out.push_back(Level{ob::to_price(s + kBaseTick), qty_[s]});
        }
        return out;
    }

    OB_API std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(static_cast<size_t>(depth));
        for (int s = best_ask_; s < kSlots && depth > 0; s = bits_.next_set(s), --depth) {
            out.push_back(Level{ob::to_price(s + kBaseTick), qty_[s]});
        }
        return out;
    }

private:
    // ---- price level storage -------------------------------------------
    static constexpr int kBaseTick = ob::kBidMinTick;                 // 5000
    static constexpr int kSlots = ob::kAskMaxTick - kBaseTick + 1;    // 10001
    static constexpr int kWords = (kSlots + 63) / 64;                 // 157
    static constexpr int kSummaryWords = (kWords + 63) / 64;          // 3

    // One bit per price slot, plus a summary with one bit per 64-bit word
    // that says "this word is non-zero".
    struct Bitmap {
        uint64_t words[kWords] = {};
        uint64_t summary[kSummaryWords] = {};

        void set(int i) {
            const int w = i >> 6;
            words[w] |= (1ULL << (i & 63));
            summary[w >> 6] |= (1ULL << (w & 63));
        }

        void clear(int i) {
            const int w = i >> 6;
            words[w] &= ~(1ULL << (i & 63));
            if (words[w] == 0) summary[w >> 6] &= ~(1ULL << (w & 63));
        }

        // Highest set bit strictly below i, or -1.
        int prev_set(int i) const {
            if (i <= 0) return -1;
            --i;
            int w = i >> 6;
            uint64_t m = words[w] & (~0ULL >> (63 - (i & 63)));
            if (m) return (w << 6) + 63 - __builtin_clzll(m);
            // not in this word, use the summary to find the previous non-zero word
            if (w == 0) return -1;
            --w;
            int sw = w >> 6;
            uint64_t sm = summary[sw] & (~0ULL >> (63 - (w & 63)));
            while (true) {
                if (sm) {
                    const int pw = (sw << 6) + 63 - __builtin_clzll(sm);
                    return (pw << 6) + 63 - __builtin_clzll(words[pw]);
                }
                if (sw == 0) return -1;
                --sw;
                sm = summary[sw];
            }
        }

        // Lowest set bit strictly above i, or kSlots.
        int next_set(int i) const {
            ++i;
            if (i >= kSlots) return kSlots;
            int w = i >> 6;
            uint64_t m = words[w] & (~0ULL << (i & 63));
            if (m) return (w << 6) + __builtin_ctzll(m);
            ++w;
            if (w >= kWords) return kSlots;
            int sw = w >> 6;
            uint64_t sm = summary[sw] & (~0ULL << (w & 63));
            while (true) {
                if (sm) {
                    const int nw = (sw << 6) + __builtin_ctzll(sm);
                    return (nw << 6) + __builtin_ctzll(words[nw]);
                }
                if (++sw >= kSummaryWords) return kSlots;
                sm = summary[sw];
            }
        }
    };

    // ---- order storage: open-addressing hash table ----------------------
    struct OrderSlot {
        uint64_t id;
        int32_t tick;  // 0 == empty slot (live ticks are always >= 5000)
        int32_t qty;
    };
    static_assert(sizeof(OrderSlot) == 16, "OrderSlot should be 16 bytes");

    static constexpr size_t kMaxLoadNum = 7;  // rehash above 70% load
    static constexpr size_t kMaxLoadDen = 10;

    static uint64_t hash_id(uint64_t x) {
        // splitmix64 finaliser, cheap and spreads sequential ids well
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return x;
    }

    size_t home(uint64_t id) const { return static_cast<size_t>(hash_id(id)) & mask_; }

    void insert_order(uint64_t id, int tick, long qty) {
        if ((count_ + 1) * kMaxLoadDen > table_.size() * kMaxLoadNum) rehash(table_.size() * 2);
        size_t i = home(id);
        while (table_[i].tick != 0) i = (i + 1) & mask_;
        table_[i] = OrderSlot{id, static_cast<int32_t>(tick), static_cast<int32_t>(qty)};
        ++count_;
    }

    bool erase_order(uint64_t id, int& tick, int& qty) {
        size_t i = home(id);
        while (true) {
            OrderSlot& s = table_[i];
            if (s.tick == 0) return false;  // hit an empty slot: not present
            if (s.id == id) break;
            i = (i + 1) & mask_;
        }
        tick = table_[i].tick;
        qty = table_[i].qty;
        --count_;

        // Backward shift delete: move later entries of the same probe run
        // back into the hole so no tombstones are needed.
        size_t hole = i;
        size_t j = (i + 1) & mask_;
        while (table_[j].tick != 0) {
            const size_t h = home(table_[j].id);
            // j can move into the hole only if its home is not in (hole, j]
            // (cyclically), otherwise a later lookup would stop too early.
            const bool in_between = (hole < j) ? (hole < h && h <= j) : (hole < h || h <= j);
            if (!in_between) {
                table_[hole] = table_[j];
                hole = j;
            }
            j = (j + 1) & mask_;
        }
        table_[hole].tick = 0;
        return true;
    }

    void rehash(size_t new_cap) {
        std::vector<OrderSlot> old;
        old.swap(table_);
        table_.assign(new_cap, OrderSlot{0, 0, 0});
        mask_ = new_cap - 1;
        count_ = 0;
        for (const OrderSlot& s : old) {
            if (s.tick != 0) insert_order(s.id, s.tick, s.qty);
        }
    }

    long qty_[kSlots] = {};
    Bitmap bits_;
    int best_bid_ = -1;      // slot index, -1 when there are no bids
    int best_ask_ = kSlots;  // slot index, kSlots when there are no asks

    std::vector<OrderSlot> table_;
    size_t mask_ = 0;
    size_t count_ = 0;
};
