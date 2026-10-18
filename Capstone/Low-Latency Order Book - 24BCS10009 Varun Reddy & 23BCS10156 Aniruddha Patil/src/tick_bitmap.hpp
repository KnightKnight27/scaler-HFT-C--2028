// Two-level occupancy bitmap over the 10,001 tick indices.
//
// words_[w] bit b is set when tick index (w*64 + b) has resting quantity.
// summary_[s] bit t is set when words_[s*64 + t] != 0.
// Finding the next occupied tick in either direction touches at most one word
// of each level plus a scan over 3 summary words, so it is O(1) no matter how
// sparse the book is.
#pragma once

#include <cstdint>

#include "common.hpp"

class TickBitmap {
public:
    void set(int i) {
        const int w = i >> 6;
        words_[w] |= bit(i);
        summary_[w >> 6] |= bit(w);
    }

    void clear(int i) {
        const int w = i >> 6;
        words_[w] &= ~bit(i);
        if (words_[w] == 0) summary_[w >> 6] &= ~bit(w);
    }

    // Highest set index <= i, or -1 if none.
    int find_prev(int i) const {
        if (i < 0) return -1;
        int w = i >> 6;
        const uint64_t here = words_[w] & up_to(i);
        if (here) return (w << 6) | highest(here);
        w = prev_word(w - 1);
        return w < 0 ? -1 : (w << 6) | highest(words_[w]);
    }

    // Lowest set index >= i, or -1 if none.
    int find_next(int i) const {
        if (i >= kBits) return -1;
        int w = i >> 6;
        const uint64_t here = words_[w] & from(i);
        if (here) return (w << 6) | lowest(here);
        w = next_word(w + 1);
        return w < 0 ? -1 : (w << 6) | lowest(words_[w]);
    }

private:
    static constexpr int kBits = px::kNumTicks;
    static constexpr int kWords = (kBits + 63) / 64;           // 157
    static constexpr int kSummaryWords = (kWords + 63) / 64;   // 3

    static uint64_t bit(int i) { return uint64_t{1} << (i & 63); }
    static uint64_t up_to(int i) { return ~uint64_t{0} >> (63 - (i & 63)); }  // bits 0..i%64
    static uint64_t from(int i) { return ~uint64_t{0} << (i & 63); }          // bits i%64..63
    static int highest(uint64_t x) { return 63 - __builtin_clzll(x); }
    static int lowest(uint64_t x) { return __builtin_ctzll(x); }

    // Highest non-empty word index <= w, or -1.
    int prev_word(int w) const {
        for (int s = w >> 6; w >= 0 && s >= 0; --s, w = s * 64 + 63) {
            const uint64_t m = summary_[s] & up_to(w);
            if (m) return (s << 6) | highest(m);
        }
        return -1;
    }

    // Lowest non-empty word index >= w, or -1.
    int next_word(int w) const {
        for (int s = w >> 6; w < kWords && s < kSummaryWords; ++s, w = s * 64) {
            const uint64_t m = summary_[s] & from(w);
            if (m) return (s << 6) | lowest(m);
        }
        return -1;
    }

    uint64_t words_[kWords] = {};
    uint64_t summary_[kSummaryWords] = {};
};
