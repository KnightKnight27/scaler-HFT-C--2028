// level_bitmap.h - a two-level bitmap over N price levels (used by implementation C).
//
// Bit i is set when level i has quantity. find_prev(i) returns the highest set bit at or
// below i in a few instructions: check i's own 64-bit word, and if that word is empty use
// the summary bitmap (one bit per non-empty word) to jump straight to the next word that
// has anything in it. For 5000 levels that is 79 words and a 2-word summary.
#pragma once

#include <cstdint>

template <int N>
class LevelBitmap {
public:
    void set(int i) {
        const int w = i >> 6;
        words_[w] |= bit(i & 63);
        summary_[w >> 6] |= bit(w & 63);
    }

    void clear(int i) {
        const int w = i >> 6;
        words_[w] &= ~bit(i & 63);
        if (words_[w] == 0) summary_[w >> 6] &= ~bit(w & 63);
    }

    bool test(int i) const { return (words_[i >> 6] & bit(i & 63)) != 0; }

    // Highest set index <= i, or -1 if there is none. i may be -1.
    int find_prev(int i) const {
        if (i < 0) return -1;
        int w = i >> 6;
        uint64_t m = words_[w] & up_to(i & 63);
        if (m != 0) return (w << 6) + highest_bit(m);

        // nothing left in this word: find the highest non-empty word below w
        if (--w < 0) return -1;
        int s = w >> 6;
        uint64_t sm = summary_[s] & up_to(w & 63);
        while (sm == 0) {
            if (--s < 0) return -1;
            sm = summary_[s];
        }
        w = (s << 6) + highest_bit(sm);
        return (w << 6) + highest_bit(words_[w]);
    }

private:
    static constexpr int kWords = (N + 63) / 64;
    static constexpr int kSummaryWords = (kWords + 63) / 64;

    static constexpr uint64_t bit(int b) { return uint64_t{1} << b; }
    static constexpr uint64_t up_to(int b) { return ~uint64_t{0} >> (63 - b); }  // bits 0..b
    static int highest_bit(uint64_t x) { return 63 - __builtin_clzll(x); }      // x != 0

    uint64_t words_[kWords] = {};
    uint64_t summary_[kSummaryWords] = {};
};
