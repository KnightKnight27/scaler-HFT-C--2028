#pragma once
// Implementation C (own design): flat tick array + two-level bitmap + open-addressing hash.
//
//   price levels : qty_[tick], one int64 per possible tick (0..15000), ~120 KB, contiguous
//   occupancy    : one bitmap with a bit per tick, plus a summary word with one bit per
//                  non-empty 64-bit word, so "next non-empty level" is two ctz/clz scans
//   best price   : best_bid_ / best_ask_ cached; only recomputed when the best level empties
//   orders       : own open-addressing hash table (linear probing, Fibonacci hashing,
//                  backward-shift deletion so there are no tombstones). Each slot holds
//                  {id, tick, qty} inline, so a cancel is one probe sequence in one array.
//                  Side is not stored: a tick <= 9999 is a bid, >= 10001 is an ask.
//
// Bids and asks share one bitmap. That is safe because the bands don't overlap:
// bid searches only ever walk down from <= 9999 and ask searches only walk up
// from >= 10001.

#include <cstring>

#include "common.hpp"

class OccupancyBitmap {
public:
    static constexpr int kBits = 15040;  // >= kMaxTick + 1, rounded up to a multiple of 64
    static constexpr int kWords = kBits / 64;                // 235
    static constexpr int kSummary = (kWords + 63) / 64;      // 4

    void set(int i) {
        const int w = i >> 6;
        words_[w] |= 1ull << (i & 63);
        summary_[w >> 6] |= 1ull << (w & 63);
    }

    void clear(int i) {
        const int w = i >> 6;
        words_[w] &= ~(1ull << (i & 63));
        if (words_[w] == 0) summary_[w >> 6] &= ~(1ull << (w & 63));
    }

    // Highest set bit <= i, or -1.
    int prev(int i) const {
        if (i < 0) return -1;
        const int w = i >> 6;
        const uint64_t m = words_[w] & (~0ull >> (63 - (i & 63)));
        if (m) return (w << 6) + 63 - __builtin_clzll(m);
        const int pw = prev_word(w - 1);
        return pw < 0 ? -1 : (pw << 6) + 63 - __builtin_clzll(words_[pw]);
    }

    // Lowest set bit >= i, or -1.
    int next(int i) const {
        if (i >= kBits) return -1;
        const int w = i >> 6;
        const uint64_t m = words_[w] & (~0ull << (i & 63));
        if (m) return (w << 6) + __builtin_ctzll(m);
        const int nw = next_word(w + 1);
        return nw < 0 ? -1 : (nw << 6) + __builtin_ctzll(words_[nw]);
    }

private:
    int prev_word(int w) const {  // highest non-empty word <= w
        if (w < 0) return -1;
        int s = w >> 6;
        uint64_t m = summary_[s] & (~0ull >> (63 - (w & 63)));
        for (;;) {
            if (m) return (s << 6) + 63 - __builtin_clzll(m);
            if (--s < 0) return -1;
            m = summary_[s];
        }
    }

    int next_word(int w) const {  // lowest non-empty word >= w
        if (w >= kWords) return -1;
        int s = w >> 6;
        uint64_t m = summary_[s] & (~0ull << (w & 63));
        for (;;) {
            if (m) return (s << 6) + __builtin_ctzll(m);
            if (++s >= kSummary) return -1;
            m = summary_[s];
        }
    }

    uint64_t words_[kWords] = {};
    uint64_t summary_[kSummary] = {};
};

// id -> {tick, qty}. Linear probing over a power-of-two array of 16-byte slots.
class OrderTable {
public:
    struct Slot {
        uint64_t id;
        int32_t tick;
        int32_t qty;
    };
    static constexpr uint64_t kEmpty = ~0ull;  // id value reserved as "empty slot"

    explicit OrderTable(size_t initial_capacity = 1u << 20) { allocate(round_pow2(initial_capacity)); }

    // false if the id is already present
    bool insert(uint64_t id, int tick, int qty) {
        if ((size_ + 1) * 10 > capacity() * 7) grow();  // keep load factor <= 0.7
        size_t i = home(id);
        while (slots_[i].id != kEmpty) {
            if (slots_[i].id == id) return false;
            i = (i + 1) & mask_;
        }
        slots_[i] = {id, tick, qty};
        ++size_;
        return true;
    }

    // Copies the order out and removes it. false if not found.
    bool erase(uint64_t id, Slot& out) {
        size_t i = home(id);
        for (;;) {
            if (slots_[i].id == kEmpty) return false;
            if (slots_[i].id == id) break;
            i = (i + 1) & mask_;
        }
        out = slots_[i];
        // Backward-shift deletion: pull later entries of the same probe run into the hole,
        // so lookups never need tombstones.
        size_t hole = i;
        size_t j = (i + 1) & mask_;
        while (slots_[j].id != kEmpty) {
            const size_t h = home(slots_[j].id);
            // Move j into the hole if its home slot is not in the cyclic range (hole, j].
            if (((j - h) & mask_) >= ((j - hole) & mask_)) {
                slots_[hole] = slots_[j];
                hole = j;
            }
            j = (j + 1) & mask_;
        }
        slots_[hole].id = kEmpty;
        --size_;
        return true;
    }

    size_t capacity() const { return mask_ + 1; }

private:
    static size_t round_pow2(size_t n) {
        size_t c = 16;
        while (c < n) c <<= 1;
        return c;
    }

    // Fibonacci hashing: multiply by 2^64/phi and keep the top bits.
    size_t home(uint64_t id) const { return (id * 11400714819323198485ull) >> shift_; }

    void allocate(size_t cap) {
        slots_.assign(cap, Slot{kEmpty, 0, 0});
        mask_ = cap - 1;
        shift_ = 64 - __builtin_ctzll(cap);
        size_ = 0;
    }

    void grow() {
        std::vector<Slot> old;
        old.swap(slots_);
        allocate(old.size() * 2);
        for (const Slot& s : old)
            if (s.id != kEmpty) insert(s.id, s.tick, s.qty);
    }

    std::vector<Slot> slots_;
    size_t mask_ = 0;
    int shift_ = 0;
    size_t size_ = 0;
};

class BookC {
public:
    explicit BookC(size_t expected_orders = 1u << 20) : orders_(expected_orders), qty_(ticks::kMaxTick + 1, 0) {}

    void add_order(uint64_t id, Side side, double price, long qty) {
        const int tick = ticks::from_price(price);
        if (qty <= 0 || qty > INT32_MAX || id == OrderTable::kEmpty) return;
        if (!ticks::in_band(side, tick) || !ticks::on_grid(price, tick)) return;
        if (!orders_.insert(id, tick, static_cast<int>(qty))) return;

        if (qty_[tick] == 0) occupied_.set(tick);
        qty_[tick] += qty;
        if (side == Side::Buy) {
            if (tick > best_bid_) best_bid_ = tick;
        } else {
            if (best_ask_ == kNoAsk || tick < best_ask_) best_ask_ = tick;
        }
    }

    bool cancel_order(uint64_t id) {
        OrderTable::Slot o;
        if (!orders_.erase(id, o)) return false;
        const int tick = o.tick;
        qty_[tick] -= o.qty;
        if (qty_[tick] == 0) {
            occupied_.clear(tick);
            if (tick == best_bid_) {
                const int p = occupied_.prev(tick - 1);
                best_bid_ = (p >= ticks::kBidMin) ? p : kNoBid;
            } else if (tick == best_ask_) {
                const int n = occupied_.next(tick + 1);
                best_ask_ = (n >= ticks::kAskMin) ? n : kNoAsk;
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (best_bid_ != kNoBid) {
            b.has_bid = true;
            b.bid_price = ticks::to_price(best_bid_);
            b.bid_qty = qty_[best_bid_];
        }
        if (best_ask_ != kNoAsk) {
            b.has_ask = true;
            b.ask_price = ticks::to_price(best_ask_);
            b.ask_qty = qty_[best_ask_];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(depth);
        for (int t = best_bid_; t >= ticks::kBidMin && (int)out.size() < depth; t = occupied_.prev(t - 1))
            out.push_back({ticks::to_price(t), qty_[t]});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(depth);
        for (int t = best_ask_; t != kNoAsk && (int)out.size() < depth; t = occupied_.next(t + 1)) {
            if (t > ticks::kAskMax) break;
            out.push_back({ticks::to_price(t), qty_[t]});
        }
        return out;
    }

private:
    static constexpr int kNoBid = -1;  // below every valid bid, so "tick > best_bid_" works on an empty side
    static constexpr int kNoAsk = -1;

    OrderTable orders_;
    std::vector<long> qty_;
    OccupancyBitmap occupied_;
    int best_bid_ = kNoBid;
    int best_ask_ = kNoAsk;
};
