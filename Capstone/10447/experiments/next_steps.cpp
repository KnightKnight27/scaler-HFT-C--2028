// extra experiment, not part of the assignment benchmark.
// asks one annoying question about BookC: what happens if the cached best price has
// to walk nearly the whole array after a cancel?
#include "../book.h"
#include <chrono>
#include <cstdio>

using Clk = std::chrono::steady_clock;

// This is only the price-presence part of the bitmap idea from README 11.6. It is
// not another complete order book and it deliberately does not include id lookup.
// One bit says whether a price level has any quantity. highest() finds the next bid
// with a few 64-bit word checks instead of walking every empty price level.
class BidBitmap {
    static constexpr int WORDS = (NL + 63) / 64;
    uint64_t bits[WORDS] = {};
public:
    void set(int i) { bits[i / 64] |= uint64_t{1} << (i % 64); }
    void clear(int i) { bits[i / 64] &= ~(uint64_t{1} << (i % 64)); }
    int highest() const {
        for (int w = WORDS - 1; w >= 0; w--) {
            if (bits[w]) return w * 64 + 63 - __builtin_clzll(bits[w]);
        }
        return -1;
    }
};

// Keep one lower order alive. Add/cancel at 9999 over and over. In the dense case the
// next non-empty tick is 9998; in sparse it is 5000, so cancel has to scan 4999 gaps.
double run(bool sparse, int rounds) {
    BookC b;
    const int lower = sparse ? BID_LO : BID_HI - 1;
    b.add_order(1, Side::Buy, lower, 1);

    auto t0 = Clk::now();
    for (int i = 0; i < rounds; i++) {
        uint64_t id = 2 + (uint64_t)i;
        b.add_order(id, Side::Buy, BID_HI, 1);
        if (!b.cancel_order(id)) std::printf("cancel failed at %d\n", i);
    }
    double ns = std::chrono::duration<double, std::nano>(Clk::now() - t0).count() / rounds;
    BBO x = b.get_bbo();
    std::printf("%s: %.1f ns/add+cancel, final bid %d x %ld\n",
                sparse ? "sparse (4999 gaps)" : "dense (1 gap)", ns, x.bid_tick, x.bid_qty);
    return ns;
}

// Same dense/sparse situation, but only measuring how the next non-empty price is
// found. Keep it separate from BookC timing: this does not pay for a hash lookup.
double run_bitmap(bool sparse, int rounds) {
    BidBitmap b;
    const int lower = sparse ? 0 : NL - 2;
    b.set(lower);
    volatile int keep = 0;

    auto t0 = Clk::now();
    for (int i = 0; i < rounds; i++) {
        b.set(NL - 1);
        b.clear(NL - 1);
        keep += b.highest();
    }
    double ns = std::chrono::duration<double, std::nano>(Clk::now() - t0).count() / rounds;
    int got = b.highest();
    std::printf("bitmap %s: %.1f ns/reselect, final index %d (keep=%d)\n",
                sparse ? "sparse" : "dense", ns, got, keep);
    return ns;
}

int main() {
    constexpr int rounds = 20000;
    std::printf("BookC cached-best scan, %d rounds\n", rounds);
    double dense = run(false, rounds);
    double sparse = run(true, rounds);
    std::printf("sparse/dense: %.1fx\n", sparse / dense);
    double bitmap_dense = run_bitmap(false, rounds);
    double bitmap_sparse = run_bitmap(true, rounds);
    std::printf("bitmap sparse/dense: %.1fx\n", bitmap_sparse / bitmap_dense);
    std::printf("lesson: array lookup is great for the normal workload, but a sparse book can make cancelling the best level O(price levels). the bitmap is the next thing worth putting into a full BookC, but this little test only proves the price-search part.\n");
}
