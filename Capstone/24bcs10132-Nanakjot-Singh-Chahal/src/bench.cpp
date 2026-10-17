// One benchmark binary for all three order books.
//
//   ./bench --impl A|B|C [--runs N] [--ops N]   benchmark one implementation
//   ./bench --verify [--ops N]                  check that A, B and C agree
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

#include "book_a_map.hpp"
#include "book_b_heap.hpp"
#include "book_c_array.hpp"
#include "workload.hpp"

using Clock = std::chrono::steady_clock;

namespace {

// ---------------------------------------------------------------- checksum
// Folds every query result into one number. Printing it stops the compiler
// from discarding the work, and equal checksums across A/B/C are a cheap
// extra sign that they behave the same.
struct Checksum {
    uint64_t h = 1469598103934665603ull;
    void mix(uint64_t v) { h = (h ^ v) * 1099511628211ull; }
    void bbo(const BBO& b) {
        mix(b.has_bid ? static_cast<uint64_t>(ticks::from_price(b.bid_price)) : 0);
        mix(static_cast<uint64_t>(b.bid_qty));
        mix(b.has_ask ? static_cast<uint64_t>(ticks::from_price(b.ask_price)) : 0);
        mix(static_cast<uint64_t>(b.ask_qty));
    }
    void levels(const std::vector<Level>& v) {
        mix(v.size());
        for (const Level& l : v) {
            mix(static_cast<uint64_t>(ticks::from_price(l.price)));
            mix(static_cast<uint64_t>(l.qty));
        }
    }
};

// --------------------------------------------------------------- benchmark
struct RunResult {
    double total_ms;      // whole workload, no per-op timers
    double add_ns;        // average per call, from the instrumented pass
    double cancel_ns;
    double bbo_ns;
    double depth_ns;      // average of one get_bids(10) or get_asks(10)
    uint64_t checksum;
    BBO final_bbo;
};

template <class Book>
uint64_t run_plain(const std::vector<Op>& ops, BBO& final_bbo) {
    Book book;
    Checksum cs;
    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        switch (op.type) {
            case OpType::Add: book.add_order(op.id, op.side, op.price, op.qty); break;
            case OpType::Cancel: cs.mix(book.cancel_order(op.id)); break;
            case OpType::Bbo: cs.bbo(book.get_bbo()); break;
        }
        if (i % 100 == 99) {
            cs.levels(book.get_bids(10));
            cs.levels(book.get_asks(10));
        }
    }
    final_bbo = book.get_bbo();
    return cs.h;
}

template <class Book>
RunResult run_once(const std::vector<Op>& ops) {
    RunResult r{};

    // Pass 1: total time with no timer calls inside the loop.
    {
        const auto t0 = Clock::now();
        r.checksum = run_plain<Book>(ops, r.final_bbo);
        const auto t1 = Clock::now();
        r.total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    }

    // Pass 2: fresh book, every call wrapped in steady_clock::now().
    Book book;
    Checksum cs;
    long long ns[3] = {0, 0, 0};
    long long cnt[3] = {0, 0, 0};
    long long depth_ns = 0, depth_cnt = 0;
    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        const int k = static_cast<int>(op.type);
        const auto t0 = Clock::now();
        switch (op.type) {
            case OpType::Add: book.add_order(op.id, op.side, op.price, op.qty); break;
            case OpType::Cancel: cs.mix(book.cancel_order(op.id)); break;
            case OpType::Bbo: cs.bbo(book.get_bbo()); break;
        }
        const auto t1 = Clock::now();
        ns[k] += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        ++cnt[k];
        if (i % 100 == 99) {
            const auto d0 = Clock::now();
            cs.levels(book.get_bids(10));
            cs.levels(book.get_asks(10));
            const auto d1 = Clock::now();
            depth_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(d1 - d0).count();
            depth_cnt += 2;
        }
    }
    auto avg = [](long long s, long long c) { return c ? static_cast<double>(s) / c : 0.0; };
    r.add_ns = avg(ns[0], cnt[0]);
    r.cancel_ns = avg(ns[1], cnt[1]);
    r.bbo_ns = avg(ns[2], cnt[2]);
    r.depth_ns = avg(depth_ns, depth_cnt);
    if (cs.h != r.checksum) std::printf("WARNING: checksum differs between passes\n");
    return r;
}

// Cost of a back-to-back pair of steady_clock::now() calls. Every per-op
// number above includes this once.
double timer_overhead_ns() {
    const int n = 1000000;
    long long s = 0;
    for (int i = 0; i < n; ++i) {
        const auto a = Clock::now();
        const auto b = Clock::now();
        s += std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
    }
    return static_cast<double>(s) / n;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

template <class Book>
int bench(const std::vector<Op>& ops, int runs) {
    std::printf("Implementation %s\n", Book::kName);
    std::printf("Operations: %zu   Runs: %d   (median reported)\n\n", ops.size(), runs);

    std::vector<double> tot, add, can, bbo, dep;
    RunResult last{};
    for (int i = 0; i < runs; ++i) {
        last = run_once<Book>(ops);
        std::printf("  run %d: total %8.2f ms | add %6.1f ns | cancel %6.1f ns | bbo %6.1f ns | depth %7.1f ns\n",
                    i + 1, last.total_ms, last.add_ns, last.cancel_ns, last.bbo_ns, last.depth_ns);
        tot.push_back(last.total_ms);
        add.push_back(last.add_ns);
        can.push_back(last.cancel_ns);
        bbo.push_back(last.bbo_ns);
        dep.push_back(last.depth_ns);
    }

    std::printf("\nMedian of %d runs\n", runs);
    std::printf("  Total time        : %.2f ms\n", median(tot));
    std::printf("  ns / add_order    : %.1f\n", median(add));
    std::printf("  ns / cancel_order : %.1f\n", median(can));
    std::printf("  ns / get_bbo      : %.1f\n", median(bbo));
    std::printf("  ns / get_bids|asks(10): %.1f\n", median(dep));
    std::printf("  (per-op numbers include ~%.1f ns of steady_clock overhead)\n", timer_overhead_ns());

    const BBO& b = last.final_bbo;
    std::printf("\nFinal BBO:\n");
    if (b.has_bid) std::printf("  Bid: %.2f x %ld\n", b.bid_price, b.bid_qty); else std::printf("  Bid: none\n");
    if (b.has_ask) std::printf("  Ask: %.2f x %ld\n", b.ask_price, b.ask_qty); else std::printf("  Ask: none\n");
    std::printf("\nChecksum: %llu\n", static_cast<unsigned long long>(last.checksum));
    return 0;
}

// ------------------------------------------------------------ verification
bool same(const BBO& x, const BBO& y) {
    if (x.has_bid != y.has_bid || x.has_ask != y.has_ask) return false;
    if (x.has_bid && (x.bid_price != y.bid_price || x.bid_qty != y.bid_qty)) return false;
    if (x.has_ask && (x.ask_price != y.ask_price || x.ask_qty != y.ask_qty)) return false;
    return true;
}

bool same(const std::vector<Level>& x, const std::vector<Level>& y) {
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); ++i)
        if (x[i].price != y[i].price || x[i].qty != y[i].qty) return false;
    return true;
}

// Brute-force reference: rebuild the levels from the raw list of live orders.
std::vector<Level> reference_levels(const std::unordered_map<uint64_t, OrderInfo>& live, Side side, int depth) {
    std::vector<long> q(ticks::kCount, 0);
    for (const auto& kv : live)
        if (kv.second.side == side) q[kv.second.tick - ticks::kMin] += kv.second.qty;
    std::vector<Level> out;
    if (side == Side::Buy) {
        for (int i = ticks::kCount - 1; i >= 0 && static_cast<int>(out.size()) < depth; --i)
            if (q[i]) out.push_back({ticks::to_price(i + ticks::kMin), q[i]});
    } else {
        for (int i = 0; i < ticks::kCount && static_cast<int>(out.size()) < depth; ++i)
            if (q[i]) out.push_back({ticks::to_price(i + ticks::kMin), q[i]});
    }
    return out;
}

// Spec edge cases, run against one implementation.
template <class Book>
bool edge_cases(std::string& why) {
    Book b;
    BBO e = b.get_bbo();
    if (e.has_bid || e.has_ask) { why = "empty book reports a side"; return false; }
    if (!b.get_bids(10).empty() || !b.get_asks(10).empty()) { why = "empty book has levels"; return false; }
    if (b.cancel_order(12345)) { why = "cancel of unknown id returned true"; return false; }

    b.add_order(1, Side::Buy, 99.50, 10);
    b.add_order(2, Side::Buy, 99.50, 5);   // same price -> qty sums to 15
    b.add_order(3, Side::Buy, 98.00, 7);
    b.add_order(4, Side::Buy, 97.25, 1);
    b.add_order(5, Side::Sell, 100.01, 3);
    b.add_order(6, Side::Sell, 150.00, 4);
    b.add_order(7, Side::Sell, 120.00, 0);  // qty 0 is ignored
    b.add_order(8, Side::Buy, 100.00, 9);   // outside bid band, ignored
    b.add_order(9, Side::Sell, 101.005, 9); // not on the 0.01 grid, ignored

    BBO x = b.get_bbo();
    if (!x.has_bid || x.bid_price != 99.50 || x.bid_qty != 15) { why = "aggregated best bid wrong"; return false; }
    if (!x.has_ask || x.ask_price != 100.01 || x.ask_qty != 3) { why = "best ask wrong"; return false; }
    if (b.get_bids(10).size() != 3) { why = "insufficient depth not handled (bids)"; return false; }
    if (b.get_asks(10).size() != 2) { why = "insufficient depth not handled (asks)"; return false; }
    if (b.get_bids(2).size() != 2) { why = "depth limit ignored"; return false; }

    if (!b.cancel_order(1)) { why = "valid cancel returned false"; return false; }
    if (b.cancel_order(1)) { why = "double cancel returned true"; return false; }
    if (b.cancel_order(7)) { why = "cancel of rejected order returned true"; return false; }
    if (b.get_bbo().bid_qty != 5) { why = "qty not reduced after cancel"; return false; }

    b.cancel_order(2);  // empties the best bid level
    x = b.get_bbo();
    if (x.bid_price != 98.00 || x.bid_qty != 7) { why = "best bid not repaired after level emptied"; return false; }
    b.cancel_order(5);
    x = b.get_bbo();
    if (x.ask_price != 150.00 || x.ask_qty != 4) { why = "best ask not repaired after level emptied"; return false; }

    b.cancel_order(3); b.cancel_order(4); b.cancel_order(6);
    x = b.get_bbo();
    if (x.has_bid || x.has_ask) { why = "book not empty after cancelling everything"; return false; }

    b.add_order(10, Side::Buy, 50.00, 2);   // extreme edges of the bands
    b.add_order(11, Side::Sell, 150.00, 2);
    x = b.get_bbo();
    if (x.bid_price != 50.00 || x.ask_price != 150.00) { why = "band edges"; return false; }
    return true;
}

int verify(const std::vector<Op>& ops) {
    std::printf("Running correctness test...\n\n");

    std::string why_a, why_b, why_c;
    const bool ea = edge_cases<MapOrderBook>(why_a);
    const bool eb = edge_cases<HeapOrderBook>(why_b);
    const bool ec = edge_cases<ArrayOrderBook>(why_c);
    std::printf("Edge cases (empty book, unknown/double cancel, depth, bands):\n");
    std::printf("  A: %s%s\n", ea ? "PASS" : "FAIL - ", why_a.c_str());
    std::printf("  B: %s%s\n", eb ? "PASS" : "FAIL - ", why_b.c_str());
    std::printf("  C: %s%s\n\n", ec ? "PASS" : "FAIL - ", why_c.c_str());

    // Random workload in lockstep. After every op: compare BBO and the top 10
    // bid/ask levels of all three. Every 10,000 ops: compare the full book and
    // also check all three against a brute-force rebuild from the live orders.
    MapOrderBook a;
    HeapOrderBook b;
    ArrayOrderBook c;
    std::unordered_map<uint64_t, OrderInfo> live;
    size_t bad[3] = {0, 0, 0};
    size_t first_bad[3] = {0, 0, 0};
    size_t checks = 0, full_checks = 0;
    auto fail = [&](int k, size_t i) { if (!bad[k]++) first_bad[k] = i; };

    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        if (op.type == OpType::Add) {
            a.add_order(op.id, op.side, op.price, op.qty);
            b.add_order(op.id, op.side, op.price, op.qty);
            c.add_order(op.id, op.side, op.price, op.qty);
            live[op.id] = OrderInfo{ticks::from_price(op.price), op.side, op.qty};
        } else if (op.type == OpType::Cancel) {
            const bool ra = a.cancel_order(op.id), rb = b.cancel_order(op.id), rc = c.cancel_order(op.id);
            if (!ra) fail(0, i);
            if (rb != ra) fail(1, i);
            if (rc != ra) fail(2, i);
            live.erase(op.id);
        }

        const BBO ba = a.get_bbo();
        if (!same(ba, b.get_bbo())) fail(1, i);
        if (!same(ba, c.get_bbo())) fail(2, i);
        const auto bids_a = a.get_bids(10), asks_a = a.get_asks(10);
        if (!same(bids_a, b.get_bids(10)) || !same(asks_a, b.get_asks(10))) fail(1, i);
        if (!same(bids_a, c.get_bids(10)) || !same(asks_a, c.get_asks(10))) fail(2, i);
        ++checks;

        if (i % 10000 == 9999 || i + 1 == ops.size()) {
            const int all = ticks::kCount;
            const auto rb = reference_levels(live, Side::Buy, all);
            const auto ra = reference_levels(live, Side::Sell, all);
            if (!same(rb, a.get_bids(all)) || !same(ra, a.get_asks(all))) fail(0, i);
            if (!same(rb, b.get_bids(all)) || !same(ra, b.get_asks(all))) fail(1, i);
            if (!same(rb, c.get_bids(all)) || !same(ra, c.get_asks(all))) fail(2, i);
            ++full_checks;
        }
    }

    std::printf("Random workload: %zu ops, %zu per-op comparisons, %zu full-book comparisons\n",
                ops.size(), checks, full_checks);
    std::printf("Final live orders: %zu, bid levels: %zu, ask levels: %zu\n\n", live.size(),
                a.get_bids(ticks::kCount).size(), a.get_asks(ticks::kCount).size());

    const char* names[3] = {"A", "B", "C"};
    const bool edge[3] = {ea, eb, ec};
    bool ok = true;
    for (int k = 0; k < 3; ++k) {
        const bool pass = edge[k] && bad[k] == 0;
        ok = ok && pass;
        if (pass)
            std::printf("Implementation %s: PASS\n", names[k]);
        else
            std::printf("Implementation %s: FAIL (%zu mismatches, first at op %zu)\n", names[k], bad[k], first_bad[k]);
    }
    std::printf(ok ? "\nAll implementations produce identical results.\n"
                   : "\nImplementations DISAGREE.\n");
    return ok ? 0 : 1;
}

void usage() {
    std::printf("usage: bench --impl A|B|C [--runs N] [--ops N]\n"
                "       bench --verify [--ops N]\n");
}

}  // namespace

int main(int argc, char** argv) {
    char impl = 0;
    bool do_verify = false;
    int runs = 5;
    size_t n_ops = 1000000;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--impl" && i + 1 < argc) impl = argv[++i][0];
        else if (a == "--verify") do_verify = true;
        else if (a == "--runs" && i + 1 < argc) runs = std::max(1, std::atoi(argv[++i]));
        else if (a == "--ops" && i + 1 < argc) n_ops = std::strtoull(argv[++i], nullptr, 10);
        else { usage(); return 2; }
    }

    const std::vector<Op> ops = make_workload(n_ops, 42);
    if (do_verify) return verify(ops);
    switch (impl) {
        case 'A': case 'a': return bench<MapOrderBook>(ops, runs);
        case 'B': case 'b': return bench<HeapOrderBook>(ops, runs);
        case 'C': case 'c': return bench<ArrayOrderBook>(ops, runs);
        default: usage(); return 2;
    }
}
