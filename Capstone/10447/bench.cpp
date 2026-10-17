// one bench for all 3 books
//   ./bench --verify              all 3 agree?
//   ./bench --impl A|B|C          5 runs, median
//   ./bench --impl A --perf       one clean untimed run, for perf stat / perf record
// g++ -std=c++17 -O2 -o bench bench.cpp
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include "book.h"

using Clk = chrono::steady_clock;

struct Op { uint8_t type; Side side; int tick; long qty; uint64_t id; };   // type: 0 add, 1 cancel, 2 bbo

template <class T> inline void keep(const T& x) { asm volatile("" ::"r"(&x) : "memory"); }

// same workload for everyone: fixed seed, built once up front (not timed)
vector<Op> make_workload(int n) {
    mt19937 rng(42);
    vector<Op> w; w.reserve(n);
    struct L { uint64_t id; long qty; };
    vector<L> live;
    uint64_t next = 1;
    for (int i = 0; i < n; i++) {
        int r = rng() % 100;
        if (r >= 70 && r < 90 && !live.empty()) {          // cancel something thats really live
            size_t k = rng() % live.size();
            L l = live[k]; live[k] = live.back(); live.pop_back();
            w.push_back({1, Side::Buy, 0, l.qty, l.id});
        } else if (r >= 90) {
            w.push_back({2, Side::Buy, 0, 0, 0});
        } else {
            Side s = (rng() & 1) ? Side::Buy : Side::Sell;
            int tick = s == Side::Buy ? BID_LO + (int)(rng() % NL) : ASK_LO + (int)(rng() % NL);
            long q = 1 + rng() % 100;
            w.push_back({0, s, tick, q, next});
            live.push_back({next, q});
            next++;
        }
    }
    return w;
}

// ------------------------------------------------------------ verify
struct Snap {
    bool cr = true; BBO bbo; vector<Level> bids, asks;
    bool operator==(const Snap& o) const { return cr == o.cr && bbo == o.bbo && bids == o.bids && asks == o.asks; }
};

template <class B> bool apply(B& b, const Op& o) {
    if (o.type == 0) b.add_order(o.id, o.side, o.tick, o.qty);
    else if (o.type == 1) return b.cancel_order(o.id);
    return true;
}
template <class B> Snap snap(B& b, bool cr, int depth) {
    Snap s; s.cr = cr; s.bbo = b.get_bbo();
    if (depth) { s.bids = b.get_bids(depth); s.asks = b.get_asks(depth); }
    return s;
}
long total_qty(const Snap& s) { long t = 0; for (auto& l : s.bids) t += l.qty; for (auto& l : s.asks) t += l.qty; return t; }

template <class B> bool edge() {
    B b; bool ok = true;
    auto chk = [&](bool c, const char* m) { if (!c) { printf("    edge case failed: %s\n", m); ok = false; } };
    BBO e = b.get_bbo();
    chk(!e.has_bid && !e.has_ask, "empty book bbo");
    chk(!b.cancel_order(123), "cancel unknown id");
    chk(b.get_bids(10).empty() && b.get_asks(10).empty(), "empty depth");
    b.add_order(1, Side::Buy, 9998, 10); b.add_order(2, Side::Buy, 9998, 5); b.add_order(3, Side::Buy, 9999, 7);
    b.add_order(4, Side::Sell, 10001, 3); b.add_order(5, Side::Sell, 10002, 4);
    BBO x = b.get_bbo();
    chk(x.has_bid && x.bid_tick == 9999 && x.bid_qty == 7 && x.has_ask && x.ask_tick == 10001 && x.ask_qty == 3, "bbo");
    auto bd = b.get_bids(10), ad = b.get_asks(10);
    chk(bd.size() == 2 && bd[0] == Level{9999, 7} && bd[1] == Level{9998, 15}, "bids, depth bigger than book");
    chk(ad.size() == 2 && ad[0] == Level{10001, 3} && ad[1] == Level{10002, 4}, "asks");
    chk(b.get_bids(1).size() == 1, "depth 1");
    chk(b.cancel_order(3), "cancel live");
    chk(!b.cancel_order(3), "cancel twice");
    x = b.get_bbo(); chk(x.bid_tick == 9998 && x.bid_qty == 15, "best bid moves after cancel");
    b.cancel_order(1); chk(b.get_bbo().bid_qty == 5, "partial level cancel");
    b.cancel_order(2); chk(!b.get_bbo().has_bid, "bid side empty again");
    b.add_order(6, Side::Buy, 10000, 5); b.add_order(7, Side::Buy, 4999, 5); b.add_order(8, Side::Sell, 10000, 5);
    chk(!b.get_bbo().has_bid, "out of range price ignored");
    b.add_order(9, Side::Buy, 7000, 5); b.add_order(9, Side::Buy, 7000, 5);
    chk(b.get_bbo().bid_qty == 5, "duplicate id ignored");
    chk(b.cancel_order(9) && !b.get_bbo().has_bid, "cancel after dup");
    b.cancel_order(4); chk(b.get_bbo().ask_tick == 10002, "best ask moves after cancel");
    return ok;
}

bool do_verify(int ops) {
    printf("Running correctness test...\n\n");
    bool bad[3] = {false, false, false};
    bad[0] |= !edge<BookA>(); bad[1] |= !edge<BookB>(); bad[2] |= !edge<BookC>();
    printf("edge cases (empty book, unknown cancel, short depth, dup id...): %s\n",
           (bad[0] || bad[1] || bad[2]) ? "FAIL" : "ok");

    auto w = make_workload(ops);
    BookA a; BookB b; BookC c;
    long live_qty = 0, checks = 0, full = 0;
    for (size_t i = 0; i < w.size(); i++) {
        const Op& o = w[i];
        int depth = (i % 100 == 99) ? 10 : 0;
        if (i % 5000 == 4999) depth = 100000;                 // whole book now and then
        bool ra = apply(a, o), rb = apply(b, o), rc = apply(c, o);
        Snap s[3] = {snap(a, ra, depth), snap(b, rb, depth), snap(c, rc, depth)};
        checks++;
        if (o.type == 0) live_qty += o.qty; else if (o.type == 1) live_qty -= o.qty;
        // majority vote, the odd one out is the buggy one
        if (s[0] == s[1]) { if (!(s[2] == s[0])) bad[2] = true; }
        else if (s[0] == s[2]) bad[1] = true;
        else if (s[1] == s[2]) bad[0] = true;
        else bad[0] = bad[1] = bad[2] = true;
        if (depth == 100000) full++;
        for (int k = 0; k < 3; k++) {
            if (!s[k].cr) bad[k] = true;                                // cancels here are always live
            if (depth == 100000 && total_qty(s[k]) != live_qty) bad[k] = true;  // book qty == live qty
        }
    }
    printf("%ld ops compared (bbo after every op, top10 every 100, full book %ld times)\n\n", checks, full);
    const char* n[3] = {"A", "B", "C"};
    for (int k = 0; k < 3; k++) printf("Implementation %s: %s\n", n[k], bad[k] ? "FAIL" : "PASS");
    bool all = !(bad[0] || bad[1] || bad[2]);
    printf("\n%s\n", all ? "All implementations produce identical results." : "MISMATCH, something is broken.");
    return all;
}

// ------------------------------------------------------------ bench
struct Stats { double total_ms = 0, ns[3] = {0, 0, 0}; uint64_t ck = 0; BBO bbo; };

// TIMED=false -> only the whole loop is timed. TIMED=true -> every add/cancel/bbo gets its own clock pair
template <class B, bool TIMED> Stats run(const vector<Op>& w, double ovh) {
    auto bk = make_unique<B>();
    uint64_t ck = 0; double acc[3] = {0, 0, 0}; long cnt[3] = {0, 0, 0};
    auto T0 = Clk::now();
    for (size_t i = 0; i < w.size(); i++) {
        const Op& o = w[i];
        Clk::time_point a;
        if constexpr (TIMED) a = Clk::now();
        BBO bb;
        switch (o.type) {
            case 0: bk->add_order(o.id, o.side, o.tick, o.qty); break;
            case 1: { bool r = bk->cancel_order(o.id); keep(r); break; }
            default: bb = bk->get_bbo(); keep(bb);
        }
        if constexpr (TIMED) { auto b = Clk::now(); acc[o.type] += chrono::duration_cast<chrono::nanoseconds>(b - a).count(); cnt[o.type]++; }
        if (o.type == 2) ck += bb.bid_tick * 31 + bb.bid_qty + bb.ask_tick * 17 + bb.ask_qty;
        if (i % 100 == 99) {                                   // every 100 ops: top 10 both sides
            auto bd = bk->get_bids(10), ad = bk->get_asks(10);
            for (auto& l : bd) ck += l.qty ^ l.tick;
            for (auto& l : ad) ck += l.qty ^ l.tick;
        }
    }
    Stats s;
    s.total_ms = chrono::duration<double, milli>(Clk::now() - T0).count();
    if constexpr (TIMED) for (int t = 0; t < 3; t++) s.ns[t] = cnt[t] ? max(0.0, acc[t] / cnt[t] - ovh) : 0;
    s.ck = ck; s.bbo = bk->get_bbo();
    return s;
}

double calib() {   // cost of one now()..now() pair, subtracted from per-op numbers
    long long s = 0;
    for (int i = 0; i < 1000000; i++) { auto a = Clk::now(); auto b = Clk::now(); s += chrono::duration_cast<chrono::nanoseconds>(b - a).count(); }
    return s / 1e6;
}
double median(vector<double> v) { sort(v.begin(), v.end()); return v[v.size() / 2]; }
void pr(const char* lbl, int tick, long q) { printf("%s: %d.%02d x %ld\n", lbl, tick / 100, tick % 100, q); }

template <class B> void do_bench(const char* name, const vector<Op>& w, int runs, bool perf) {
    printf("Implementation %s, %zu ops\n", name, w.size());
    Stats last;
    if (perf) {
        last = run<B, false>(w, 0);
        printf("(perf mode: one untimed run)\n");
    } else {
        double ovh = calib();
        vector<double> tot, ns[3];
        for (int r = 0; r < runs; r++) {
            Stats s1 = run<B, false>(w, 0), s2 = run<B, true>(w, ovh);
            if (s1.ck != s2.ck) printf("warning: checksum differs between passes!\n");
            tot.push_back(s1.total_ms);
            for (int t = 0; t < 3; t++) ns[t].push_back(s2.ns[t]);
            last = s1;
        }
        printf("median of %d runs (timer overhead %.1f ns/op subtracted)\n", runs, ovh);
        printf("  total      %9.2f ms\n  ns / add   %9.1f\n  ns / cancel%9.1f\n  ns / bbo   %9.1f\n", median(tot), median(ns[0]), median(ns[1]), median(ns[2]));
    }
    printf("Final BBO:\n");
    if (last.bbo.has_bid) pr("Bid", last.bbo.bid_tick, last.bbo.bid_qty);
    if (last.bbo.has_ask) pr("Ask", last.bbo.ask_tick, last.bbo.ask_qty);
    printf("\nChecksum: %llu\n", (unsigned long long)last.ck);
}

int main(int argc, char** argv) {
    string impl; int runs = 5, ops = 1000000; bool verify = false, perf = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--impl") && i + 1 < argc) impl = argv[++i];
        else if (!strcmp(argv[i], "--runs") && i + 1 < argc) runs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ops") && i + 1 < argc) ops = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--verify")) verify = true;
        else if (!strcmp(argv[i], "--perf")) perf = true;
    }
    if (verify) return do_verify(ops) ? 0 : 1;
    if (impl != "A" && impl != "B" && impl != "C") { printf("usage: ./bench --verify | --impl A|B|C [--runs N] [--perf] [--ops N]\n"); return 1; }
    auto w = make_workload(ops);
    if (impl == "A") do_bench<BookA>("A (std::map + unordered_map)", w, runs, perf);
    if (impl == "B") do_bench<BookB>("B (sorted vector + std::map)", w, runs, perf);
    if (impl == "C") do_bench<BookC>("C (price array + flat hash)", w, runs, perf);
}
