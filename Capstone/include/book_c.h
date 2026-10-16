// book_c.h — Implementation C: direct-indexed levels + two-level bitmap + open-addressing orders.
//
// Structure:
//   levels_  : std::vector<LevelSlot>(10,001), one slot per tick 5000..15000, index = tick - 5000.
//              Bids and asks share the array; index 5000 (price 100.00) is never used.
//   words_   : 157 x 64-bit words, bit i set <=> levels_[i] is non-empty
//   summary_ : 3 x 64-bit words, bit w set <=> words_[w] != 0
//   best_bid_/best_ask_ : cached index of the best non-empty level, or kNoLevel
//   slots_   : open-addressing hash table id -> {qty, tick}, linear probing,
//              power-of-two capacity, Fibonacci hashing, backward-shift deletion
//
// Finding the next best level: when the best level empties, search the bitmap
// downward (bids) or upward (asks). That is one masked word, then at most 3 summary
// words, then one word, each resolved with a single count-leading/trailing-zeros
// instruction. Worst case is a handful of word operations, never a 5,000-slot scan.
// Bid searches only move down from a bid index and ask searches only up from an ask
// index, so one shared bitmap never mixes the two sides.
//
// Order store details:
//   - A slot is empty iff qty == 0 (live orders have qty >= 1). No id value is reserved,
//     so ids 0 and UINT64_MAX work.
//   - Side is not stored: tick < 10000 <=> Buy. Slot = 8 (id) + 4 (qty) + 4 (tick) = 16 B,
//     four slots per 64-byte cache line.
//   - Precondition beyond the API: qty <= INT32_MAX (asserted in debug builds).
//   - Default capacity 2^21 slots (32 MB), allocated and zeroed in the constructor, so its
//     page faults happen at construction, not during the run. The table doubles if the load
//     factor would exceed 0.5. That never happens in the graded workload (~501k live orders
//     -> load ~0.24), but it keeps the book correct for any input.
//   - Backward-shift deletion: after removing a slot, later entries of the same probe chain
//     are moved back into the hole, so there are no tombstones and lookups never slow down.
//
// Invariants (checked by check_invariants(), O(capacity + ticks), tests/verify only):
//   - Each level's {qty, count} equals the sum over the live orders at that tick.
//   - words_ bit i <=> levels_[i].count > 0; summary_ bit w <=> words_[w] != 0.
//   - best_bid_/best_ask_ equal the true highest bid / lowest ask index (or kNoLevel).
//   - size_ equals the number of occupied slots, and every occupied slot is reachable
//     from its home position (find_slot returns it), i.e. no probe chain is broken.
//
// Complexities (N = live orders, d = min(depth, levels on that side)):
//   add_order     O(1) expected (hash insert; O(N) only when the table doubles)
//   cancel_order  O(1) expected (hash find + backward shift, array update,
//                 bitmap search of <= 5 words if the best level emptied)
//   get_bbo       O(1)
//   get_bids/asks O(d): each next level is one bitmap search
#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "order_book_iface.h"
#include "types.h"

#if !defined(__GNUC__)
#error "book_c.h uses __builtin_clzll/__builtin_ctzll (GCC/Clang). C++17 has no portable equivalent."
#endif

class BookC {
public:
    static constexpr std::size_t kDefaultCapacity = std::size_t{1} << 21;

    explicit BookC(std::size_t capacity = kDefaultCapacity) : levels_(kNumTicks) { reset_table(capacity); }

    void add_order(uint64_t id, Side side, double price, long qty) {
        const Tick tick = price_to_tick(price);
        assert(is_valid_tick(side, tick));
        assert(qty >= 1 && qty <= INT32_MAX);

        insert_order(id, static_cast<int32_t>(qty), tick);

        const int idx = tick - kMinTick;
        LevelSlot& lvl = levels_[static_cast<std::size_t>(idx)];
        if (lvl.count == 0) {
            set_bit(idx);
            if (side == Side::Buy) {
                if (best_bid_ == kNoLevel || idx > best_bid_) best_bid_ = idx;
            } else {
                if (best_ask_ == kNoLevel || idx < best_ask_) best_ask_ = idx;
            }
        }
        lvl.qty += qty;
        lvl.count += 1;
    }

    bool cancel_order(uint64_t id) {
        const std::size_t pos = find_slot(id);
        if (pos == kNotFound) return false;  // unknown or already cancelled

        const Slot order = slots_[pos];  // copy before the backward shift overwrites it
        erase_slot(pos);

        const int idx = order.tick - kMinTick;
        LevelSlot& lvl = levels_[static_cast<std::size_t>(idx)];
        lvl.qty -= order.qty;
        lvl.count -= 1;
        assert(lvl.qty >= 0 && lvl.count >= 0);
        if (lvl.count == 0) {
            assert(lvl.qty == 0);
            clear_bit(idx);
            // Only an emptied BEST level moves the cached best; any other level is irrelevant.
            if (idx == best_bid_) {
                best_bid_ = find_prev(idx - 1);
            } else if (idx == best_ask_) {
                best_ask_ = find_next(idx + 1);
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};
        if (best_bid_ != kNoLevel) {
            bbo.has_bid = true;
            bbo.bid_price = tick_to_price(kMinTick + best_bid_);
            bbo.bid_qty = levels_[static_cast<std::size_t>(best_bid_)].qty;
        }
        if (best_ask_ != kNoLevel) {
            bbo.has_ask = true;
            bbo.ask_price = tick_to_price(kMinTick + best_ask_);
            bbo.ask_qty = levels_[static_cast<std::size_t>(best_ask_)].qty;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        assert(depth >= 0);
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(static_cast<std::size_t>(std::min(depth, kMaxDepthReserve)));
        for (int i = best_bid_; i != kNoLevel && static_cast<int>(out.size()) < depth; i = find_prev(i - 1)) {
            out.push_back(Level{tick_to_price(kMinTick + i), levels_[static_cast<std::size_t>(i)].qty});
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        assert(depth >= 0);
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(static_cast<std::size_t>(std::min(depth, kMaxDepthReserve)));
        for (int i = best_ask_; i != kNoLevel && static_cast<int>(out.size()) < depth; i = find_next(i + 1)) {
            out.push_back(Level{tick_to_price(kMinTick + i), levels_[static_cast<std::size_t>(i)].qty});
        }
        return out;
    }

    bool check_invariants() const {
        // Rebuild every level from the hash table and check every slot is reachable.
        std::vector<LevelSlot> expected(kNumTicks);
        std::size_t used = 0;
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            const Slot& s = slots_[i];
            if (s.qty == 0) continue;
            ++used;
            if (s.qty < 0 || s.tick < kMinTick || s.tick > kMaxTick || s.tick == kMidTick) return false;
            if (find_slot(s.id) != i) return false;  // broken probe chain or duplicate id
            LevelSlot& lvl = expected[static_cast<std::size_t>(s.tick - kMinTick)];
            lvl.qty += s.qty;
            lvl.count += 1;
        }
        if (used != size_) return false;

        int true_best_bid = kNoLevel;
        int true_best_ask = kNoLevel;
        for (int i = 0; i < kNumTicks; ++i) {
            const LevelSlot& lvl = levels_[static_cast<std::size_t>(i)];
            const LevelSlot& exp = expected[static_cast<std::size_t>(i)];
            if (lvl.qty != exp.qty || lvl.count != exp.count) return false;
            if (test_bit(i) != (lvl.count > 0)) return false;
            if (lvl.count > 0) {
                if (kMinTick + i < kMidTick) true_best_bid = i;  // ascending scan: last bid wins
                else if (true_best_ask == kNoLevel) true_best_ask = i;
            }
        }
        for (int w = 0; w < kWords; ++w) {
            const bool summary_bit = (summary_[static_cast<std::size_t>(w >> 6)] >> (w & 63)) & 1u;
            if (summary_bit != (words_[static_cast<std::size_t>(w)] != 0)) return false;
        }
        return best_bid_ == true_best_bid && best_ask_ == true_best_ask;
    }

private:
    struct LevelSlot {
        long qty = 0;    // sum of live order qtys at this tick
        long count = 0;  // number of live orders at this tick
    };

    struct Slot {
        uint64_t id = 0;
        int32_t qty = 0;  // 0 marks an empty slot
        Tick tick = 0;    // side is implied: tick < kMidTick <=> Buy
    };
    static_assert(sizeof(Slot) == 16, "four hash slots per cache line");

    static constexpr int kNoLevel = -1;
    static constexpr int kWords = (kNumTicks + 63) / 64;     // 157
    static constexpr int kSummaryWords = (kWords + 63) / 64;  // 3
    static constexpr int kMaxDepthReserve = 64;  // cap on up-front reserve for huge depths
    static constexpr std::size_t kNotFound = SIZE_MAX;

    // --- bitmap -----------------------------------------------------------------

    // Bits 0..b inclusive / bits b..63 inclusive.
    static uint64_t mask_up_to(int b) { return b == 63 ? ~uint64_t{0} : (uint64_t{1} << (b + 1)) - 1; }
    static uint64_t mask_from(int b) { return ~uint64_t{0} << b; }
    static int highest_bit(uint64_t x) { return 63 - __builtin_clzll(x); }  // x != 0 (clz(0) is UB)
    static int lowest_bit(uint64_t x) { return __builtin_ctzll(x); }        // x != 0 (ctz(0) is UB)

    void set_bit(int i) {
        const int w = i >> 6;
        words_[static_cast<std::size_t>(w)] |= uint64_t{1} << (i & 63);
        summary_[static_cast<std::size_t>(w >> 6)] |= uint64_t{1} << (w & 63);
    }

    void clear_bit(int i) {
        const int w = i >> 6;
        words_[static_cast<std::size_t>(w)] &= ~(uint64_t{1} << (i & 63));
        if (words_[static_cast<std::size_t>(w)] == 0) {
            summary_[static_cast<std::size_t>(w >> 6)] &= ~(uint64_t{1} << (w & 63));
        }
    }

    bool test_bit(int i) const { return (words_[static_cast<std::size_t>(i >> 6)] >> (i & 63)) & 1u; }

    // Highest non-empty index <= i, or kNoLevel.
    int find_prev(int i) const {
        if (i < 0) return kNoLevel;
        const int w = i >> 6;
        const uint64_t bits = words_[static_cast<std::size_t>(w)] & mask_up_to(i & 63);
        if (bits != 0) return (w << 6) + highest_bit(bits);

        // No hit in i's own word: find the highest non-empty word below it via the summary.
        const int below = w - 1;
        if (below < 0) return kNoLevel;
        int s = below >> 6;
        uint64_t summary_bits = summary_[static_cast<std::size_t>(s)] & mask_up_to(below & 63);
        while (summary_bits == 0) {
            if (s == 0) return kNoLevel;
            summary_bits = summary_[static_cast<std::size_t>(--s)];
        }
        const int word = (s << 6) + highest_bit(summary_bits);
        return (word << 6) + highest_bit(words_[static_cast<std::size_t>(word)]);
    }

    // Lowest non-empty index >= i, or kNoLevel.
    int find_next(int i) const {
        if (i >= kNumTicks) return kNoLevel;
        const int w = i >> 6;
        const uint64_t bits = words_[static_cast<std::size_t>(w)] & mask_from(i & 63);
        if (bits != 0) return (w << 6) + lowest_bit(bits);

        // No hit in i's own word: find the lowest non-empty word above it via the summary.
        const int above = w + 1;
        if (above >= kWords) return kNoLevel;
        int s = above >> 6;
        uint64_t summary_bits = summary_[static_cast<std::size_t>(s)] & mask_from(above & 63);
        while (summary_bits == 0) {
            if (++s >= kSummaryWords) return kNoLevel;
            summary_bits = summary_[static_cast<std::size_t>(s)];
        }
        const int word = (s << 6) + lowest_bit(summary_bits);
        return (word << 6) + lowest_bit(words_[static_cast<std::size_t>(word)]);
    }

    // --- open-addressing hash table -------------------------------------------

    void reset_table(std::size_t capacity) {
        std::size_t cap = 2;
        int bits = 1;
        while (cap < capacity) {
            cap <<= 1;
            ++bits;
        }
        slots_.assign(cap, Slot{});
        mask_ = cap - 1;
        shift_ = 64 - bits;
        size_ = 0;
    }

    // Fibonacci hashing: multiply by 2^64/phi and keep the top bits. Spreads sequential
    // ids evenly across the table, unlike taking the low bits of the id directly.
    std::size_t home(uint64_t id) const {
        return static_cast<std::size_t>((id * 0x9E3779B97F4A7C15ull) >> shift_);
    }

    std::size_t find_slot(uint64_t id) const {
        for (std::size_t i = home(id);; i = (i + 1) & mask_) {
            const Slot& s = slots_[i];
            if (s.qty == 0) return kNotFound;  // reached the end of the probe chain
            if (s.id == id) return i;
        }
    }

    void insert_order(uint64_t id, int32_t qty, Tick tick) {
        if ((size_ + 1) * 2 > slots_.size()) grow();
        std::size_t i = home(id);
        while (slots_[i].qty != 0) {
            assert(slots_[i].id != id && "order ids must be unique");
            i = (i + 1) & mask_;
        }
        slots_[i] = Slot{id, qty, tick};
        ++size_;
    }

    // Backward-shift deletion. Walk the probe chain after the hole. An entry may move
    // back into the hole only if its home position is NOT cyclically within (hole, j]:
    // if it were, moving it before its home would make it unreachable.
    void erase_slot(std::size_t hole) {
        for (std::size_t j = (hole + 1) & mask_; slots_[j].qty != 0; j = (j + 1) & mask_) {
            const std::size_t k = home(slots_[j].id);
            const bool home_in_range = hole <= j ? (hole < k && k <= j) : (hole < k || k <= j);
            if (home_in_range) continue;
            slots_[hole] = slots_[j];
            hole = j;
        }
        slots_[hole] = Slot{};
        --size_;
    }

    void grow() {
        std::vector<Slot> old;
        old.swap(slots_);
        reset_table(old.size() * 2);
        for (const Slot& s : old) {
            if (s.qty != 0) insert_order(s.id, s.qty, s.tick);
        }
    }

    std::vector<LevelSlot> levels_;
    std::array<uint64_t, kWords> words_{};
    std::array<uint64_t, kSummaryWords> summary_{};
    int best_bid_ = kNoLevel;  // index into levels_
    int best_ask_ = kNoLevel;

    std::vector<Slot> slots_;
    std::size_t mask_ = 0;
    int shift_ = 0;
    std::size_t size_ = 0;
};

static_assert(implements_order_book<BookC>(), "BookC must satisfy the order book interface");
