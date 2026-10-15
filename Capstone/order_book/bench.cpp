// bench.cpp - ONE program that benchmarks and verifies all three order books.
//
//   g++ -O2 -std=c++17 bench.cpp -o bench
//
//   ./bench --verify                  correctness test: A, B, C vs each other and vs a naive reference
//   ./bench --impl A|B|C|all          benchmark (5 runs, median), same workload for every impl
//   ./bench --impl A --perf --reps 10 plain loop only (no per-op timers) - meant for `perf stat`
//   ./bench --impl none --perf        workload generation only (baseline for perf, see README)
//
// Options: --ops N (1000000)  --seed S (42)  --runs N (5)  --reps N (1)
//          --workload uniform|clustered (uniform = the spec)
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "book_a.hpp"
#include "book_b.hpp"
#include "book_c.hpp"
#include "common.hpp"
#include "workload.hpp"

using Clock = std::chrono::steady_clock;

// ------------------------------------------------------------------------------------------
// checksum: folds every result into one number so the compiler cannot delete the work, and so
// all implementations can be compared with a single value.
// ------------------------------------------------------------------------------------------
static inline std::uint64_t mix(std::uint64_t h, std::uint64_t v) {
    return (h ^ v) * 0x100000001b3ULL;
}
static inline std::uint64_t mix_bbo(std::uint64_t h, const BBO& b) {
    h = mix(h, b.has_bid);
    h = mix(h, static_cast<std::uint64_t>(b.bid_price));
    h = mix(h, static_cast<std::uint64_t>(b.bid_qty));
    h = mix(h, b.has_ask);
    h = mix(h, static_cast<std::uint64_t>(b.ask_price));
    return mix(h, static_cast<std::uint64_t>(b.ask_qty));
}
static inline std::uint64_t mix_levels(std::uint64_t h, const std::vector<Level>& v) {
    for (const Level& l : v) {
        h = mix(h, static_cast<std::uint64_t>(l.price));
        h = mix(h, static_cast<std::uint64_t>(l.qty));
    }
    return h;
}

// ------------------------------------------------------------------------------------------
// Plain pass: no timers inside the loop. This gives "total time" and is what `perf` profiles.
// ------------------------------------------------------------------------------------------
template <class Book>
static std::uint64_t play(Book& book, const std::vector<Op>& ops, BBO* final_bbo = nullptr) {
    std::uint64_t chk = 0;
    for (std::size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        switch (op.type) {
            case OpType::Add:
                chk = mix(chk, book.add_order(op.id, op.side, op.price, op.qty));
                break;
            case OpType::Cancel:
                chk = mix(chk, book.cancel_order(op.id));
                break;
            case OpType::Bbo:
                chk = mix_bbo(chk, book.get_bbo());
                break;
        }
        if ((i + 1) % 100 == 0) {  // every 100 operations: depth-10 snapshot of both sides
            chk = mix_levels(chk, book.get_bids(10));
            chk = mix_levels(chk, book.get_asks(10));
        }
    }
    if (final_bbo) *final_bbo = book.get_bbo();
    return chk;
}

// ------------------------------------------------------------------------------------------
// Timed pass: every call is bracketed by steady_clock reads, so each operation type can be
// measured separately. The cost of the two clock reads is calibrated and subtracted.
// ------------------------------------------------------------------------------------------
struct Sample {
    double        total_ms = 0;
    double        ns_add = 0, ns_cancel = 0, ns_bbo = 0, ns_depth = 0;
    std::uint64_t checksum = 0;
    BBO           final_bbo{};
};

constexpr int kBboBatch = 32;  // get_bbo() calls timed together (see run_once)

static double clock_overhead_ns() {
    constexpr int kN = 2'000'000;
    std::int64_t acc = 0;
    for (int i = 0; i < kN; ++i) {
        const auto t0 = Clock::now();
        const auto t1 = Clock::now();
        acc += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    }
    return static_cast<double>(acc) / kN;
}

template <class Book>
static Sample run_once(const std::vector<Op>& ops, double overhead_ns) {
    Sample s;
    {  // pass 1: total time, no per-op timers
        auto book = std::make_unique<Book>();
        const auto t0 = Clock::now();
        s.checksum = play(*book, ops, &s.final_bbo);
        const auto t1 = Clock::now();
        s.total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
    {  // pass 2: per-operation-type timers on a fresh book
        auto book = std::make_unique<Book>();
        std::int64_t t_add = 0, t_cancel = 0, t_bbo = 0, t_depth = 0;
        std::size_t  n_add = 0, n_cancel = 0, n_bbo = 0, n_depth = 0;
        std::uint64_t chk = 0;
        auto ns = [](Clock::time_point a, Clock::time_point b) {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
        };
        for (std::size_t i = 0; i < ops.size(); ++i) {
            const Op& op = ops[i];
            switch (op.type) {
                case OpType::Add: {
                    const auto a = Clock::now();
                    const bool ok = book->add_order(op.id, op.side, op.price, op.qty);
                    const auto b = Clock::now();
                    t_add += ns(a, b); ++n_add;
                    chk = mix(chk, ok);
                    break;
                }
                case OpType::Cancel: {
                    const auto a = Clock::now();
                    const bool ok = book->cancel_order(op.id);
                    const auto b = Clock::now();
                    t_cancel += ns(a, b); ++n_cancel;
                    chk = mix(chk, ok);
                    break;
                }
                case OpType::Bbo: {
                    // get_bbo() takes ~1 ns, far below the clock's resolution, so time a batch of
                    // back-to-back calls and divide. The empty asm with a "memory" clobber stops the
                    // compiler from merging the identical const calls into one.
                    BBO bb{};
                    const auto a = Clock::now();
                    for (int k = 0; k < kBboBatch; ++k) {
                        bb = book->get_bbo();
                        asm volatile("" : : "m"(bb) : "memory");
                    }
                    const auto b = Clock::now();
                    t_bbo += ns(a, b); ++n_bbo;  // n_bbo counts batches
                    chk = mix_bbo(chk, bb);
                    break;
                }
            }
            if ((i + 1) % 100 == 0) {
                const auto a = Clock::now();
                const std::vector<Level> bids = book->get_bids(10);
                const std::vector<Level> asks = book->get_asks(10);
                const auto b = Clock::now();
                t_depth += ns(a, b); ++n_depth;
                chk = mix_levels(mix_levels(chk, bids), asks);
            }
        }
        auto per = [&](std::int64_t t, std::size_t n) {
            return n ? static_cast<double>(t) / static_cast<double>(n) - overhead_ns : 0.0;
        };
        s.ns_add = per(t_add, n_add);
        s.ns_cancel = per(t_cancel, n_cancel);
        s.ns_bbo = n_bbo ? (static_cast<double>(t_bbo) / static_cast<double>(n_bbo) - overhead_ns) / kBboBatch : 0.0;
        s.ns_depth = per(t_depth, n_depth);
        if (chk != s.checksum) {
            std::fprintf(stderr, "internal error: timed pass checksum differs from plain pass\n");
            std::exit(2);
        }
    }
    return s;
}

static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

static std::string fmt_bbo(const BBO& b) {
    char buf[160];
    char bid[48], ask[48];
    if (b.has_bid) std::snprintf(bid, sizeof bid, "%.2f x %ld", to_price(b.bid_price), b.bid_qty);
    else           std::snprintf(bid, sizeof bid, "none");
    if (b.has_ask) std::snprintf(ask, sizeof ask, "%.2f x %ld", to_price(b.ask_price), b.ask_qty);
    else           std::snprintf(ask, sizeof ask, "none");
    std::snprintf(buf, sizeof buf, "Bid: %s\n  Ask: %s", bid, ask);
    return buf;
}

struct Summary {
    std::string name;
    double total_ms, ns_add, ns_cancel, ns_bbo, ns_depth;
    std::uint64_t checksum;
};

template <class Book>
static Summary benchmark(const char* label, const std::vector<Op>& ops, int runs, double overhead) {
    std::vector<double> tot, add, can, bbo, dep;
    Sample last;
    for (int r = 0; r < runs; ++r) {
        last = run_once<Book>(ops, overhead);
        tot.push_back(last.total_ms);
        add.push_back(last.ns_add);
        can.push_back(last.ns_cancel);
        bbo.push_back(last.ns_bbo);
        dep.push_back(last.ns_depth);
    }
    Summary s{label, median(tot), median(add), median(can), median(bbo), median(dep), last.checksum};
    std::printf("\nImplementation %s\n", label);
    std::printf("  total time (median of %d): %.2f ms\n", runs, s.total_ms);
    std::printf("  ns / add_order           : %.1f\n", s.ns_add);
    std::printf("  ns / cancel_order        : %.1f\n", s.ns_cancel);
    std::printf("  ns / get_bbo             : %.1f\n", s.ns_bbo);
    std::printf("  ns / depth pair (get_bids(10)+get_asks(10), every 100 ops): %.1f\n", s.ns_depth);
    std::printf("  Final BBO:\n  %s\n", fmt_bbo(last.final_bbo).c_str());
    std::printf("  Checksum: %llu\n", static_cast<unsigned long long>(s.checksum));
    return s;
}

// perf mode: just the plain loop, `reps` times on fresh books. No clock calls in the loop.
template <class Book>
static void perf_mode(const char* label, const std::vector<Op>& ops, int reps) {
    std::uint64_t chk = 0;
    BBO fb{};
    for (int r = 0; r < reps; ++r) {
        auto book = std::make_unique<Book>();
        chk = play(*book, ops, &fb);
    }
    std::printf("Implementation %s (perf mode, %d rep%s)\nFinal BBO:\n  %s\nChecksum: %llu\n", label,
                reps, reps == 1 ? "" : "s", fmt_bbo(fb).c_str(), static_cast<unsigned long long>(chk));
}

// ------------------------------------------------------------------------------------------
// Correctness
// ------------------------------------------------------------------------------------------

// Deliberately naive reference: keeps only the live orders and rebuilds the levels from scratch
// on every query. Slow but obviously correct.
class Reference {
public:
    bool add_order(OrderId id, Side s, Price px, long qty) {
        if (qty <= 0 || !valid_price(s, px)) return false;
        return orders_.emplace(id, Rec{s, px, qty}).second;
    }
    bool cancel_order(OrderId id) { return orders_.erase(id) == 1; }
    std::vector<Level> get_bids(int depth) const { return side_levels(Side::Buy, depth); }
    std::vector<Level> get_asks(int depth) const { return side_levels(Side::Sell, depth); }
    BBO get_bbo() const {
        BBO b{};
        const auto bids = get_bids(1), asks = get_asks(1);
        if (!bids.empty()) { b.has_bid = true; b.bid_price = bids[0].price; b.bid_qty = bids[0].qty; }
        if (!asks.empty()) { b.has_ask = true; b.ask_price = asks[0].price; b.ask_qty = asks[0].qty; }
        return b;
    }

private:
    struct Rec { Side side; Price price; long qty; };
    std::vector<Level> side_levels(Side s, int depth) const {
        std::map<Price, long> m;
        for (const auto& kv : orders_)
            if (kv.second.side == s) m[kv.second.price] += kv.second.qty;
        std::vector<Level> out;
        if (s == Side::Buy) {
            for (auto it = m.rbegin(); it != m.rend() && static_cast<int>(out.size()) < depth; ++it)
                out.push_back(Level{it->first, it->second});
        } else {
            for (auto it = m.begin(); it != m.end() && static_cast<int>(out.size()) < depth; ++it)
                out.push_back(Level{it->first, it->second});
        }
        return out;
    }
    std::map<OrderId, Rec> orders_;
};

// Hand-written edge cases from the spec. Returns the number of failed checks.
template <class Book>
static int directed_tests(const char* name) {
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { ++fails; std::printf("  [%s] FAILED: %s\n", name, what); }
    };
    {   // empty book
        Book b;
        const BBO x = b.get_bbo();
        check(!x.has_bid && !x.has_ask, "empty book: has_bid and has_ask are false");
        check(b.get_bids().empty() && b.get_asks().empty(), "empty book: no depth levels");
        check(!b.cancel_order(1), "cancel on empty book returns false (no crash)");
    }
    {   // invalid input is rejected
        Book b;
        check(!b.add_order(1, Side::Buy, 10000, 5), "buy at mid price 100.00 rejected");
        check(!b.add_order(2, Side::Buy, 4999, 5), "buy below 50.00 rejected");
        check(!b.add_order(3, Side::Sell, 10000, 5), "sell at mid price rejected");
        check(!b.add_order(4, Side::Sell, 15001, 5), "sell above 150.00 rejected");
        check(!b.add_order(5, Side::Buy, 9999, 0), "zero quantity rejected");
        check(b.add_order(6, Side::Buy, 5000, 1) && b.add_order(7, Side::Buy, 9999, 1) &&
              b.add_order(8, Side::Sell, 10001, 1) && b.add_order(9, Side::Sell, 15000, 1),
              "band edges 50.00 / 99.99 / 100.01 / 150.00 accepted");
    }
    {   // aggregation, ordering, depth, cancel
        Book b;
        b.add_order(1, Side::Buy, 9999, 10);
        b.add_order(2, Side::Buy, 9999, 5);
        b.add_order(3, Side::Buy, 9998, 7);
        b.add_order(4, Side::Sell, 10001, 4);
        b.add_order(5, Side::Sell, 10001, 6);
        b.add_order(6, Side::Sell, 10003, 1);
        check(!b.add_order(3, Side::Buy, 9990, 1), "duplicate live order id rejected");
        BBO x = b.get_bbo();
        check(x.has_bid && x.bid_price == 9999 && x.bid_qty == 15, "best bid qty is the SUM at that price");
        check(x.has_ask && x.ask_price == 10001 && x.ask_qty == 10, "best ask qty is the SUM at that price");
        auto bids = b.get_bids(10);
        auto asks = b.get_asks(10);
        check(bids.size() == 2 && bids[0] == Level{9999, 15} && bids[1] == Level{9998, 7},
              "bids returned best (highest) first");
        check(asks.size() == 2 && asks[0] == Level{10001, 10} && asks[1] == Level{10003, 1},
              "asks returned best (lowest) first");
        check(b.get_bids(1).size() == 1, "depth limit respected");
        check(b.get_asks(10).size() == 2, "insufficient depth returns the levels that exist");
        check(b.cancel_order(1), "cancel live order returns true");
        check(!b.cancel_order(1), "cancel an already-cancelled order returns false");
        check(!b.cancel_order(12345), "cancel unknown order returns false");
        x = b.get_bbo();
        check(x.bid_price == 9999 && x.bid_qty == 5, "cancel reduces the level quantity");
        check(b.cancel_order(2), "cancel last order at the best bid");
        x = b.get_bbo();
        check(x.has_bid && x.bid_price == 9998 && x.bid_qty == 7, "best bid moves to the next level");
        check(b.cancel_order(4) && b.cancel_order(5), "cancel both orders at the best ask");
        x = b.get_bbo();
        check(x.has_ask && x.ask_price == 10003 && x.ask_qty == 1, "best ask moves to the next level");
        check(b.cancel_order(3) && b.cancel_order(6), "cancel everything");
        x = b.get_bbo();
        check(!x.has_bid && !x.has_ask, "book is empty again");
    }
    return fails;
}

struct VerifyResult {
    long mismatches[4] = {0, 0, 0, 0};  // vs the naive reference; [3] = C built with a tiny hash table
    long cross = 0;                     // A vs B vs C vs C-tiny compared directly
};

// Replays `ops` through A, B, C (+ a tiny-hash-table C) and the naive reference.
// After EVERY operation: BBO and the top 10 levels of both sides must agree across all of them.
// Every `ref_every` operations: the complete book (all price levels) is compared with the reference.
static VerifyResult verify_workload(const std::vector<Op>& ops, std::size_t ref_every) {
    auto a = std::make_unique<BookA>();
    auto b = std::make_unique<BookB>();
    auto c = std::make_unique<BookC>();
    auto c2 = std::make_unique<BookC>(16);  // tiny table: forces many rehashes + long probe chains
    Reference ref;
    VerifyResult res;
    auto report = [&](int which, std::size_t i, const char* what) {
        if (++res.mismatches[which] <= 3)
            std::printf("  mismatch: impl %s, op #%zu: %s\n",
                        which == 3 ? "C-tiny" : std::string(1, static_cast<char>('A' + which)).c_str(), i, what);
    };

    for (std::size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        bool ra = true, rb = true, rc = true, rc2 = true, rr = true;
        if (op.type == OpType::Add) {
            ra = a->add_order(op.id, op.side, op.price, op.qty);
            rb = b->add_order(op.id, op.side, op.price, op.qty);
            rc = c->add_order(op.id, op.side, op.price, op.qty);
            rc2 = c2->add_order(op.id, op.side, op.price, op.qty);
            rr = ref.add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == OpType::Cancel) {
            ra = a->cancel_order(op.id);
            rb = b->cancel_order(op.id);
            rc = c->cancel_order(op.id);
            rc2 = c2->cancel_order(op.id);
            rr = ref.cancel_order(op.id);
        }
        if (ra != rr) report(0, i, "return value");
        if (rb != rr) report(1, i, "return value");
        if (rc != rr) report(2, i, "return value");
        if (rc2 != rr) report(3, i, "return value");

        const BBO ba = a->get_bbo(), bb = b->get_bbo(), bc = c->get_bbo(), bc2 = c2->get_bbo();
        if (ba != bb || ba != bc || ba != bc2) ++res.cross;
        const auto bids = a->get_bids(10), asks = a->get_asks(10);
        if (bids != b->get_bids(10) || bids != c->get_bids(10) || bids != c2->get_bids(10) ||
            asks != b->get_asks(10) || asks != c->get_asks(10) || asks != c2->get_asks(10))
            ++res.cross;

        if ((i + 1) % ref_every == 0 || i + 1 == ops.size()) {
            const BBO br = ref.get_bbo();
            if (ba != br) report(0, i, "BBO");
            if (bb != br) report(1, i, "BBO");
            if (bc != br) report(2, i, "BBO");
            if (bc2 != br) report(3, i, "BBO");
            const auto rbids = ref.get_bids(kLevelsPerSide), rasks = ref.get_asks(kLevelsPerSide);
            if (a->get_bids(kLevelsPerSide) != rbids || a->get_asks(kLevelsPerSide) != rasks)
                report(0, i, "full book (all levels)");
            if (b->get_bids(kLevelsPerSide) != rbids || b->get_asks(kLevelsPerSide) != rasks)
                report(1, i, "full book (all levels)");
            if (c->get_bids(kLevelsPerSide) != rbids || c->get_asks(kLevelsPerSide) != rasks)
                report(2, i, "full book (all levels)");
            if (c2->get_bids(kLevelsPerSide) != rbids || c2->get_asks(kLevelsPerSide) != rasks)
                report(3, i, "full book (all levels)");
        }
    }
    return res;
}

static int run_verify(std::size_t n_ops, std::uint32_t seed, PriceModel model) {
    std::printf("Running correctness test...\n");
    const int fails_directed[3] = {directed_tests<BookA>("A"), directed_tests<BookB>("B"),
                                   directed_tests<BookC>("C")};

    // Phase 1: the benchmark workload (70/20/10). The book only grows here, so best levels
    // rarely empty. Phase 2: churn (48% add / 47% cancel / 5% BBO, prices near the touch), where
    // price levels fill and empty constantly - this exercises "find the next best price".
    const std::vector<Op> bench_ops = make_workload(n_ops, seed, model);
    const std::vector<Op> churn_ops = make_workload(std::max<std::size_t>(n_ops / 5, 1000), seed + 1,
                                                    PriceModel::Clustered, 48, 47);
    std::printf("  phase 1: %zu ops of the benchmark workload\n", bench_ops.size());
    const VerifyResult p1 = verify_workload(bench_ops, 1000);
    std::printf("  phase 2: %zu ops of churn workload (levels empty and refill constantly)\n", churn_ops.size());
    const VerifyResult p2 = verify_workload(churn_ops, 200);

    bool all_pass = (p1.cross == 0 && p2.cross == 0);
    for (int k = 0; k < 3; ++k) {
        const long mm = p1.mismatches[k] + p2.mismatches[k];
        const bool pass = fails_directed[k] == 0 && mm == 0;
        all_pass = all_pass && pass;
        std::printf("Implementation %c: %s  (edge-case failures: %d, workload mismatches vs reference: %ld)\n",
                    'A' + k, pass ? "PASS" : "FAIL", fails_directed[k], mm);
    }
    const long mm3 = p1.mismatches[3] + p2.mismatches[3];
    all_pass = all_pass && mm3 == 0;
    std::printf("Implementation C started with a 16-slot hash table (forces rehash + collision paths): %s  (mismatches: %ld)\n",
                mm3 == 0 ? "PASS" : "FAIL", mm3);
    std::printf("  BBO + top-10 of both sides compared across A/B/C after every operation;\n"
                "  complete book (every price level) compared with a naive reference periodically.\n");
    if (all_pass) std::printf("\nAll implementations produce identical results.\n");
    else          std::printf("\nMISMATCH between implementations (cross-implementation diffs: %ld).\n",
                              p1.cross + p2.cross);
    return all_pass ? 0 : 1;
}

// ------------------------------------------------------------------------------------------
static void usage() {
    std::printf(
        "usage: bench --verify\n"
        "       bench --impl A|B|C|all|none [--runs N] [--ops N] [--seed S]\n"
        "                                   [--workload uniform|clustered] [--perf] [--reps N]\n");
}

int main(int argc, char** argv) {
    std::string impl;
    bool verify = false, perf = false;
    int runs = 5, reps = 1;
    std::size_t n_ops = 1'000'000;
    std::uint32_t seed = 42;
    PriceModel model = PriceModel::Uniform;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--verify") verify = true;
        else if (a == "--perf") perf = true;
        else if (a == "--impl") impl = next();
        else if (a == "--runs") runs = std::atoi(next());
        else if (a == "--reps") reps = std::atoi(next());
        else if (a == "--ops") n_ops = std::strtoull(next(), nullptr, 10);
        else if (a == "--seed") seed = static_cast<std::uint32_t>(std::strtoul(next(), nullptr, 10));
        else if (a == "--workload") model = (std::string(next()) == "clustered") ? PriceModel::Clustered
                                                                               : PriceModel::Uniform;
        else { usage(); return 1; }
    }
    if (!verify && impl.empty()) { usage(); return 1; }
    if (runs < 1) runs = 1;
    if (reps < 1) reps = 1;

    if (verify) return run_verify(n_ops, seed, model);

    const std::vector<Op> ops = make_workload(n_ops, seed, model);
    std::printf("workload: %zu ops, seed %u, %s prices (70%% add / 20%% cancel / 10%% BBO, depth-10 snapshot every 100 ops)\n",
                ops.size(), seed, model == PriceModel::Uniform ? "uniform" : "clustered");

    if (impl == "none") {  // baseline: generation only, so perf counters can be corrected for it
        std::printf("generated %zu ops (no order book used)\n", ops.size());
        return 0;
    }

    const bool a = impl == "A" || impl == "all";
    const bool b = impl == "B" || impl == "all";
    const bool c = impl == "C" || impl == "all";
    if (!a && !b && !c) { usage(); return 1; }

    if (perf) {
        if (a) perf_mode<BookA>("A", ops, reps);
        if (b) perf_mode<BookB>("B", ops, reps);
        if (c) perf_mode<BookC>("C", ops, reps);
        return 0;
    }

    const double overhead = clock_overhead_ns();
    std::printf("steady_clock read-pair overhead: %.1f ns (subtracted from per-op numbers)\n", overhead);
    std::vector<Summary> all;
    if (a) all.push_back(benchmark<BookA>("A", ops, runs, overhead));
    if (b) all.push_back(benchmark<BookB>("B", ops, runs, overhead));
    if (c) all.push_back(benchmark<BookC>("C", ops, runs, overhead));

    if (all.size() > 1) {
        std::printf("\n| Metric (median of %d runs) |", runs);
        for (const auto& s : all) std::printf(" Implementation %s |", s.name.c_str());
        std::printf("\n|---|");
        for (std::size_t i = 0; i < all.size(); ++i) std::printf("---|");
        auto row = [&](const char* name, double Summary::*f) {
            std::printf("\n| %s |", name);
            for (const auto& s : all) std::printf(" %.2f |", s.*f);
        };
        row("Total time (ms)", &Summary::total_ms);
        row("ns / add", &Summary::ns_add);
        row("ns / cancel", &Summary::ns_cancel);
        row("ns / BBO", &Summary::ns_bbo);
        row("ns / depth pair", &Summary::ns_depth);
        std::printf("\n");
        bool same = true;
        for (const auto& s : all) same = same && s.checksum == all[0].checksum;
        std::printf("\nchecksums %s across implementations\n", same ? "MATCH" : "DIFFER");
    }
    return 0;
}
