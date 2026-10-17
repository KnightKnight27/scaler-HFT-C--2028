// one driver for all the dev experiments, picks the header with -DHDR='"vN.h"'
// modes: time | stale | chain | stress N
#include HDR
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
using Clk = chrono::steady_clock;

int main(int argc, char** argv) {
    string m = argc > 1 ? argv[1] : "";
    if (m == "time") {                      // 100k mixed ops, 70% add / 30% random cancel
        BookC b; mt19937 rng(42); uint64_t id = 1; vector<uint64_t> live;
        auto t0 = Clk::now();
        for (int i = 0; i < 100000; i++) {
            if (rng() % 100 < 70 || live.empty()) {
                Side s = (rng() & 1) ? Side::Buy : Side::Sell;
                int tick = s == Side::Buy ? BID_LO + (int)(rng() % NL) : ASK_LO + (int)(rng() % NL);
                b.add_order(id, s, tick, 1 + rng() % 100); live.push_back(id++);
            } else {
                size_t k = rng() % live.size();
                b.cancel_order(live[k]); live[k] = live.back(); live.pop_back();
            }
        }
        printf("100k ops: %.1f ms\n", chrono::duration<double, milli>(Clk::now() - t0).count());
    } else if (m == "stale") {              // cancel the only order at the best bid
        BookC b;
        b.add_order(1, Side::Buy, 9999, 10);
        b.add_order(2, Side::Buy, 9998, 5);
        b.cancel_order(1);
        BBO x = b.get_bbo();
        printf("bbo after cancel: has_bid=%d tick=%d qty=%ld   (want has_bid=1 tick=9998 qty=5)\n", x.has_bid, x.bid_tick, x.bid_qty);
    } else if (m == "chain") {              // 3 ids in the same probe chain, cancel the middle one, find the last
        auto h = [](uint64_t id) -> size_t {
#ifdef HASH_HI
            return (id * 0x9E3779B97F4A7C15ull) >> (64 - 12);
#else
            return (id * 0x9E3779B97F4A7C15ull) & ((1 << 12) - 1);
#endif
        };
        uint64_t a = 1, b2 = 0, c2 = 0;
        for (uint64_t x = 2; !b2; x++) if (h(x) == h(a)) b2 = x;
        for (uint64_t x = b2 + 1; !c2; x++) if (h(x) == h(a)) c2 = x;
        BookC b;
        b.add_order(a, Side::Buy, 9999, 1); b.add_order(b2, Side::Buy, 9998, 1); b.add_order(c2, Side::Buy, 9997, 1);
        b.cancel_order(b2);
        printf("colliding ids %llu %llu %llu, cancel(last) -> %d (want 1)\n", (unsigned long long)a, (unsigned long long)b2, (unsigned long long)c2, (int)b.cancel_order(c2));
    } else if (m == "stress") {             // add+cancel cycles, table only has 4096 slots
        long n = argc > 2 ? atol(argv[2]) : 20000;
        BookC b; uint64_t id = 1;
        for (long i = 0; i < n; i++) { b.add_order(id, Side::Buy, 9999, 1); b.cancel_order(id); id++; }
        printf("%ld add/cancel cycles done\n", n);
    } else { printf("modes: time | stale | chain | stress N\n"); return 1; }
}
