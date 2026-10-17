// Single benchmark + correctness program for all three order-book designs.
//
//   ./bench --impl A|B|C [--runs 5] [--ops 1000000] [--seed 42]
//   ./bench --impl A|B|C --perf          (one uninstrumented pass only; for perf stat/record)
//   ./bench --gen-only   [--ops N] [--seed S]   (workload generation only; perf baseline)
//   ./bench --verify     [--ops 1000000] [--seed 42]
//
// Timing uses std::chrono::steady_clock. Each run performs two passes over the
// identical pre-generated workload:
//   1. an *uninstrumented* pass (one timer around the whole loop) -> total time
//   2. an *instrumented* pass (timer around every call) -> mean ns per op type
//      and latency percentiles
// so the per-op numbers do not pollute the total-time figure. The cost a timer
// pair adds to every sample (an empty timed interval) is measured and printed,
// and the "net" means subtract it.
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/resource.h>

#include "book_a_map.hpp"
#include "book_b_vector.hpp"
#include "book_c_array.hpp"
#include "reference_book.hpp"
#include "workload.hpp"

using Clock = std::chrono::steady_clock;

static inline uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
}

// Page faults (minor + major) taken by this process so far. Sampling it around
// each phase splits faults into workload generation, book construction and the
// hot loop, which a whole-process perf stat count cannot do.
static long page_faults() {
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_minflt + ru.ru_majflt;
}

// Compiler barriers for the get_bbo burst (same idea as Google Benchmark's
// DoNotOptimize/ClobberMemory). escape() makes the book's address visible to
// an opaque asm statement and the "memory" clobber says that statement may
// have written to it, so get_bbo() has to be re-run every iteration instead of
// being hoisted out of the loop as loop-invariant.
static inline void escape(const void* p) { asm volatile("" : : "g"(p) : "memory"); }

// ----------------------------------------------------------------------------
// Checksum helpers (keep the compiler honest and give --verify something to compare)
// ----------------------------------------------------------------------------
static inline uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}
static inline uint64_t fold_bbo(uint64_t h, const BBO& b) {
    h = mix(h, b.has_bid ? static_cast<uint64_t>(to_tick(b.bid_price)) * 1000003ull + b.bid_qty : 1);
    h = mix(h, b.has_ask ? static_cast<uint64_t>(to_tick(b.ask_price)) * 1000003ull + b.ask_qty : 2);
    return h;
}
static inline uint64_t fold_levels(uint64_t h, const std::vector<Level>& v) {
    h = mix(h, v.size());
    for (const Level& l : v) h = mix(h, static_cast<uint64_t>(to_tick(l.price)) * 1000003ull + l.qty);
    return h;
}

// Cheap fold for the get_bbo burst. fold_bbo() calls lround() twice through
// to_tick(), and on its own costs ~3x more than get_bbo(), so a burst built on
// it mostly timed the checksum. This one folds the raw price bits instead.
static inline uint64_t bits_of(double d) {
    uint64_t u;
    std::memcpy(&u, &d, sizeof u);
    return u;
}
static inline uint64_t fold_bbo_raw(uint64_t h, const BBO& b) {
    return h + (bits_of(b.bid_price) ^ static_cast<uint64_t>(b.bid_qty) ^ b.has_bid) +
           (bits_of(b.ask_price) ^ static_cast<uint64_t>(b.ask_qty) ^ b.has_ask);
}

// ----------------------------------------------------------------------------
// Benchmark
// ----------------------------------------------------------------------------
struct Pct {
    double p50 = 0, p90 = 0, p99 = 0, p999 = 0, max = 0;
};

// Latency percentiles of per-call samples in ns (nearest-rank). Sorts `v`.
static Pct percentiles(std::vector<uint32_t>& v) {
    Pct p;
    if (v.empty()) return p;
    std::sort(v.begin(), v.end());
    auto at = [&](double q) { return static_cast<double>(v[static_cast<size_t>(q * (v.size() - 1))]); };
    p.p50 = at(0.50);
    p.p90 = at(0.90);
    p.p99 = at(0.99);
    p.p999 = at(0.999);
    p.max = v.back();
    return p;
}

static inline uint32_t sample(uint64_t ns) { return ns > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(ns); }

struct RunResult {
    double total_ms = 0;        // uninstrumented pass
    double ns_add = 0, ns_cancel = 0, ns_bbo = 0, ns_depth = 0;  // instrumented pass, means
    Pct p_add, p_cancel, p_bbo, p_depth;                          // instrumented pass, percentiles
    double ns_bbo_burst = 0;    // 1M back-to-back get_bbo() calls, timed as a block
    long faults_construct = 0;  // page faults while constructing the book (pass 1)
    long faults_loop = 0;       // page faults during the timed loop (pass 1)
    uint64_t checksum = 0;
    BBO final_bbo{};
};

template <class Book>
static RunResult run_once(const Workload& w) {
    RunResult r;

    // ---- pass 1: whole-loop timing ------------------------------------
    {
        const long f0 = page_faults();
        Book book;
        const long f1 = page_faults();
        uint64_t chk = 0;
        const uint64_t t0 = now_ns();
        for (const Op& op : w.ops) {
            switch (op.type) {
                case OpType::Add: book.add_order_ticks(op.id, op.side, op.tick, op.qty); break;
                case OpType::Cancel: chk += book.cancel_order(op.id); break;
                case OpType::Bbo: chk = fold_bbo(chk, book.get_bbo()); break;
                case OpType::Depth:
                    chk = fold_levels(chk, book.get_bids(10));
                    chk = fold_levels(chk, book.get_asks(10));
                    break;
            }
        }
        const uint64_t t1 = now_ns();
        r.faults_construct = f1 - f0;
        r.faults_loop = page_faults() - f1;
        r.total_ms = static_cast<double>(t1 - t0) / 1e6;
        r.checksum = fold_bbo(chk, book.get_bbo());
        r.final_bbo = book.get_bbo();

        // get_bbo is far cheaper than a clock read, so additionally time a
        // burst of 1M back-to-back get_bbo() calls on the final book state.
        const int kBurst = 1'000'000;
        uint64_t hb = 0;
        const uint64_t b0 = now_ns();
        for (int i = 0; i < kBurst; ++i) {
            escape(&book);  // force a real get_bbo() every iteration
            hb = fold_bbo_raw(hb, book.get_bbo());
        }
        const uint64_t b1 = now_ns();
        r.ns_bbo_burst = static_cast<double>(b1 - b0) / kBurst;
        if (hb == 0x1234) std::printf("(unlikely)\n");  // keep the loop alive
    }

    // ---- pass 2: per-operation timing ---------------------------------
    {
        Book book;
        uint64_t chk = 0;
        uint64_t t_add = 0, t_cancel = 0, t_bbo = 0, t_depth = 0;
        // One sample per call, for percentiles. Reserved up front so recording
        // never allocates; the push happens after the second clock read.
        std::vector<uint32_t> s_add, s_cancel, s_bbo, s_depth;
        s_add.reserve(w.n_add);
        s_cancel.reserve(w.n_cancel);
        s_bbo.reserve(w.n_bbo);
        s_depth.reserve(w.n_depth);
        for (const Op& op : w.ops) {
            switch (op.type) {
                case OpType::Add: {
                    const uint64_t a = now_ns();
                    book.add_order_ticks(op.id, op.side, op.tick, op.qty);
                    const uint64_t d = now_ns() - a;
                    t_add += d;
                    s_add.push_back(sample(d));
                    break;
                }
                case OpType::Cancel: {
                    const uint64_t a = now_ns();
                    chk += book.cancel_order(op.id);
                    const uint64_t d = now_ns() - a;
                    t_cancel += d;
                    s_cancel.push_back(sample(d));
                    break;
                }
                case OpType::Bbo: {
                    const uint64_t a = now_ns();
                    const BBO b = book.get_bbo();
                    const uint64_t d = now_ns() - a;
                    t_bbo += d;
                    s_bbo.push_back(sample(d));
                    chk = fold_bbo(chk, b);
                    break;
                }
                case OpType::Depth: {
                    const uint64_t a = now_ns();
                    const std::vector<Level> bids = book.get_bids(10);
                    const std::vector<Level> asks = book.get_asks(10);
                    const uint64_t d = now_ns() - a;
                    t_depth += d;
                    s_depth.push_back(sample(d));
                    chk = fold_levels(chk, bids);
                    chk = fold_levels(chk, asks);
                    break;
                }
            }
        }
        const uint64_t chk2 = fold_bbo(chk, book.get_bbo());
        if (chk2 != r.checksum) {
            std::fprintf(stderr, "internal error: passes disagree (%llu vs %llu)\n",
                         (unsigned long long)r.checksum, (unsigned long long)chk2);
            std::exit(2);
        }
        r.ns_add = w.n_add ? static_cast<double>(t_add) / w.n_add : 0;
        r.ns_cancel = w.n_cancel ? static_cast<double>(t_cancel) / w.n_cancel : 0;
        r.ns_bbo = w.n_bbo ? static_cast<double>(t_bbo) / w.n_bbo : 0;
        r.ns_depth = w.n_depth ? static_cast<double>(t_depth) / w.n_depth : 0;
        r.p_add = percentiles(s_add);
        r.p_cancel = percentiles(s_cancel);
        r.p_bbo = percentiles(s_bbo);
        r.p_depth = percentiles(s_depth);
    }
    return r;
}

static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// Cost a steady_clock::now() pair adds to every per-call sample: the interval
// between two back-to-back reads, sampled exactly like a timed call. (The wall
// time per iteration of this loop is about twice that, because each iteration
// makes two reads; an earlier version subtracted that figure and so
// over-corrected every "net" mean by one clock read.)
struct TimerCost {
    double mean = 0;
    Pct pct;
};

static TimerCost timer_cost() {
    const int n = 1'000'000;
    std::vector<uint32_t> s;
    s.reserve(n);
    uint64_t sum = 0;
    for (int i = 0; i < n; ++i) {
        const uint64_t a = now_ns();
        const uint64_t d = now_ns() - a;
        sum += d;
        s.push_back(sample(d));
    }
    TimerCost t;
    t.mean = static_cast<double>(sum) / n;
    t.pct = percentiles(s);
    return t;
}

static void print_final(const BBO& b, uint64_t checksum) {
    std::printf("\nFinal BBO:\n");
    if (b.has_bid) std::printf("  Bid: %.2f x %ld\n", b.bid_price, b.bid_qty); else std::printf("  Bid: (none)\n");
    if (b.has_ask) std::printf("  Ask: %.2f x %ld\n", b.ask_price, b.ask_qty); else std::printf("  Ask: (none)\n");
    std::printf("\nChecksum: %llu\n", (unsigned long long)checksum);
}

// --perf: exactly one uninstrumented pass, no per-op timers, no bbo burst, so
// that `perf stat` / `perf record` counters are dominated by the book itself.
template <class Book>
static int run_perf(const char* name, const Workload& w, long gen_faults) {
    const long f0 = page_faults();
    Book book;
    const long f1 = page_faults();
    uint64_t chk = 0;
    const uint64_t t0 = now_ns();
    for (const Op& op : w.ops) {
        switch (op.type) {
            case OpType::Add: book.add_order_ticks(op.id, op.side, op.tick, op.qty); break;
            case OpType::Cancel: chk += book.cancel_order(op.id); break;
            case OpType::Bbo: chk = fold_bbo(chk, book.get_bbo()); break;
            case OpType::Depth:
                chk = fold_levels(chk, book.get_bids(10));
                chk = fold_levels(chk, book.get_asks(10));
                break;
        }
    }
    const uint64_t t1 = now_ns();
    const long f2 = page_faults();
    const BBO b = book.get_bbo();
    std::printf("Implementation %s (--perf: single pass, %zu ops): %.2f ms\n", name, w.ops.size(),
                static_cast<double>(t1 - t0) / 1e6);
    std::printf("Page faults: workload generation %ld | book construction %ld | timed loop %ld\n", gen_faults,
                f1 - f0, f2 - f1);
    print_final(b, fold_bbo(chk, b));
    return 0;
}

template <class Book>
static int run_bench(const char* name, const Workload& w, int runs, long gen_faults) {
    std::printf("Implementation %s  (%zu ops: %zu add, %zu cancel, %zu bbo, +%zu depth queries; %d runs)\n",
                name, w.n_add + w.n_cancel + w.n_bbo, w.n_add, w.n_cancel, w.n_bbo, w.n_depth, runs);
    const TimerCost timer = timer_cost();
    std::vector<double> total, add, cancel, bbo, depth, burst;
    std::vector<Pct> p_add, p_cancel, p_bbo, p_depth;
    RunResult first{}, last{};
    for (int i = 0; i < runs; ++i) {
        last = run_once<Book>(w);
        if (i == 0) first = last;
        total.push_back(last.total_ms);
        add.push_back(last.ns_add);
        cancel.push_back(last.ns_cancel);
        bbo.push_back(last.ns_bbo);
        depth.push_back(last.ns_depth);
        burst.push_back(last.ns_bbo_burst);
        p_add.push_back(last.p_add);
        p_cancel.push_back(last.p_cancel);
        p_bbo.push_back(last.p_bbo);
        p_depth.push_back(last.p_depth);
        std::printf("  run %d: total %.2f ms | add %.1f ns | cancel %.1f ns | bbo %.1f ns | depth(10+10) %.1f ns"
                    " | bbo burst %.2f ns\n",
                    i + 1, last.total_ms, last.ns_add, last.ns_cancel, last.ns_bbo, last.ns_depth,
                    last.ns_bbo_burst);
    }
    const double ovh = timer.mean;
    auto net = [&](double raw) { return raw > ovh ? raw - ovh : 0.0; };
    std::printf("\nMedian of %d runs (impl %s):          raw   net of timer cost\n", runs, name);
    std::printf("  Total time        : %10.2f ms   (uninstrumented pass, %zu ops)\n", median(total), w.ops.size());
    std::printf("  ns / add_order    : %10.1f   %10.1f\n", median(add), net(median(add)));
    std::printf("  ns / cancel_order : %10.1f   %10.1f\n", median(cancel), net(median(cancel)));
    std::printf("  ns / get_bbo      : %10.1f   %10.1f   (per-call timing; near timer resolution)\n",
                median(bbo), net(median(bbo)));
    std::printf("  ns / get_bbo burst: %10.2f              (1M back-to-back calls on final book, timed as a block)\n",
                median(burst));
    std::printf("  ns / depth query  : %10.1f   %10.1f   (get_bids(10) + get_asks(10))\n", median(depth),
                net(median(depth)));
    std::printf("  timer cost        : %10.1f ns   (mean empty timed interval; subtracted for \"net\")\n", ovh);

    // Median over runs of each percentile. Samples include the timer cost, so
    // the "timer only" row is the floor under every other row.
    auto row = [&](const char* label, const std::vector<Pct>& ps) {
        auto med = [&](double Pct::*field) {
            std::vector<double> v;
            for (const Pct& p : ps) v.push_back(p.*field);
            return median(v);
        };
        std::printf("  %-14s %8.0f %8.0f %8.0f %8.0f %10.0f\n", label, med(&Pct::p50), med(&Pct::p90),
                    med(&Pct::p99), med(&Pct::p999), med(&Pct::max));
    };
    std::printf("\nPer-call latency percentiles, ns (raw, timer cost included; median of %d runs):\n", runs);
    std::printf("  %-14s %8s %8s %8s %8s %10s\n", "", "p50", "p90", "p99", "p99.9", "max");
    row("add_order", p_add);
    row("cancel_order", p_cancel);
    row("get_bbo", p_bbo);
    row("depth (10+10)", p_depth);
    row("timer only", std::vector<Pct>{timer.pct});

    std::printf("\nPage faults (run 1): workload generation %ld | book construction %ld | timed loop %ld\n",
                gen_faults, first.faults_construct, first.faults_loop);
    print_final(last.final_bbo, last.checksum);
    return 0;
}

// ----------------------------------------------------------------------------
// Correctness: lock-step comparison of all three books with a reference model
// ----------------------------------------------------------------------------
static bool same_levels(const std::vector<Level>& a, const std::vector<Level>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

template <class Book>
static bool edge_cases(const char* name) {
    bool ok = true;
    Book book;
    BBO e = book.get_bbo();
    if (e.has_bid || e.has_ask) { std::printf("  [%s] empty book BBO reported a side\n", name); ok = false; }
    if (!book.get_bids(10).empty() || !book.get_asks(10).empty()) { std::printf("  [%s] empty book depth not empty\n", name); ok = false; }
    if (book.cancel_order(12345)) { std::printf("  [%s] cancel of unknown id returned true\n", name); ok = false; }

    book.add_order(1, Side::Buy, 99.98, 450);
    book.add_order(2, Side::Buy, 99.98, 50);
    book.add_order(3, Side::Buy, 99.50, 10);
    book.add_order(4, Side::Buy, 60.00, 7);
    book.add_order(5, Side::Sell, 100.01, 320);
    book.add_order(6, Side::Sell, 120.00, 1);
    BBO b = book.get_bbo();
    if (!(b.has_bid && to_tick(b.bid_price) == 9998 && b.bid_qty == 500 && b.has_ask && to_tick(b.ask_price) == 10001 && b.ask_qty == 320)) {
        std::printf("  [%s] BBO wrong after adds\n", name); ok = false;
    }
    std::vector<Level> bids = book.get_bids(10);
    if (bids.size() != 3 || to_tick(bids[0].price) != 9998 || bids[0].qty != 500 || to_tick(bids[1].price) != 9950 || to_tick(bids[2].price) != 6000) {
        std::printf("  [%s] get_bids(10) with 3 levels wrong (size %zu)\n", name, bids.size()); ok = false;
    }
    if (book.get_asks(1).size() != 1) { std::printf("  [%s] get_asks(1) size wrong\n", name); ok = false; }
    if (book.get_bids(0).size() != 0 || book.get_asks(-3).size() != 0 || book.get_bids(2147483647).size() != 3) {
        std::printf("  [%s] zero / negative / huge depth handled wrong\n", name); ok = false;
    }
    if (!book.cancel_order(1)) { std::printf("  [%s] cancel of live order returned false\n", name); ok = false; }
    if (book.cancel_order(1)) { std::printf("  [%s] double cancel returned true\n", name); ok = false; }
    b = book.get_bbo();
    if (!(b.has_bid && to_tick(b.bid_price) == 9998 && b.bid_qty == 50)) { std::printf("  [%s] BBO wrong after partial level cancel\n", name); ok = false; }
    book.cancel_order(2);
    b = book.get_bbo();
    if (!(b.has_bid && to_tick(b.bid_price) == 9950 && b.bid_qty == 10)) { std::printf("  [%s] BBO did not fall to next level\n", name); ok = false; }
    book.cancel_order(5);
    b = book.get_bbo();
    if (!(b.has_ask && to_tick(b.ask_price) == 12000 && b.ask_qty == 1)) { std::printf("  [%s] ask BBO did not fall to next level\n", name); ok = false; }
    book.cancel_order(3); book.cancel_order(4); book.cancel_order(6);
    b = book.get_bbo();
    if (b.has_bid || b.has_ask) { std::printf("  [%s] book not empty after cancelling everything\n", name); ok = false; }
    // level re-use after it was emptied
    book.add_order(7, Side::Sell, 100.01, 3);
    b = book.get_bbo();
    if (!(b.has_ask && to_tick(b.ask_price) == 10001 && b.ask_qty == 3)) { std::printf("  [%s] re-adding to an emptied level failed\n", name); ok = false; }
    book.cancel_order(7);

    // invalid input must be rejected and leave the book untouched
    const bool rejected =
        !book.add_order(10, Side::Buy, 120.00, 5) &&    // buy priced in the ask band
        !book.add_order(11, Side::Sell, 99.00, 5) &&    // sell priced in the bid band
        !book.add_order(12, Side::Buy, 49.99, 5) &&     // below bid band
        !book.add_order(13, Side::Sell, 150.01, 5) &&   // above ask band
        !book.add_order(14, Side::Buy, 100.00, 5) &&    // the mid price is in neither band
        !book.add_order(15, Side::Buy, 99.00, 0) &&     // zero quantity
        !book.add_order(16, Side::Sell, 101.00, -5) &&  // negative quantity
        !book.add_order(17, Side::Buy, 42949772.95, 5) &&  // 2^32 + 9999 ticks: used to wrap to 99.99
        !book.add_order(18, Side::Sell, 100.013, 5) &&  // not a multiple of the 0.01 tick
        !book.add_order(19, Side::Buy, std::nan(""), 5);  // not a number
    b = book.get_bbo();
    if (!rejected || b.has_bid || b.has_ask || book.cancel_order(10)) {
        std::printf("  [%s] invalid order was accepted\n", name); ok = false;
    }
    if (!book.add_order(20, Side::Buy, 99.00, 1) || book.add_order(20, Side::Buy, 98.00, 1)) {
        std::printf("  [%s] duplicate id handling wrong\n", name); ok = false;
    }
    // the largest uint64_t id is a legal id (C stores it outside its hash table)
    const uint64_t max_id = ~0ull;
    const bool max_added = book.add_order(max_id, Side::Sell, 150.00, 2);
    const std::vector<Level> top_ask = book.get_asks(1);
    if (!max_added || book.add_order(max_id, Side::Sell, 149.00, 2) || top_ask.size() != 1 ||
        to_tick(top_ask[0].price) != 15000 || top_ask[0].qty != 2 || !book.cancel_order(max_id) ||
        book.cancel_order(max_id) || book.get_bbo().has_ask) {
        std::printf("  [%s] id 2^64-1 handling wrong\n", name); ok = false;
    }
    // a whole-number price is a price (75.00), not a tick count
    if (!book.add_order(21, Side::Buy, 75, 4) || book.get_bids(10).back().price != 75.0) {
        std::printf("  [%s] whole-number price handled wrong\n", name); ok = false;
    }
    return ok;
}

// One book's agreement with the reference after an operation: the return
// value of the add/cancel, the BBO, and every level of the 10-deep bid and
// ask ladders.
struct Expected {
    bool ret;
    BBO bbo;
    std::vector<Level> bids, asks;
};

template <class Book>
static bool step_matches(Book& book, const Op& op, const Expected& e) {
    bool ret = e.ret;
    if (op.type == OpType::Add) ret = book.add_order_ticks(op.id, op.side, op.tick, op.qty);
    else if (op.type == OpType::Cancel) ret = book.cancel_order(op.id);
    return ret == e.ret && book.get_bbo() == e.bbo && same_levels(book.get_bids(10), e.bids) &&
           same_levels(book.get_asks(10), e.asks);
}

static int run_verify(const Workload& w) {
    std::printf("Running correctness test...\n\n");
    std::printf("Edge cases (empty book, unknown/double cancel, insufficient depth, invalid input):\n");
    bool okA = edge_cases<OrderBookA>("A");
    bool okB = edge_cases<OrderBookB>("B");
    bool okC = edge_cases<OrderBookC>("C");
    std::printf("  A: %s   B: %s   C: %s\n", okA ? "PASS" : "FAIL", okB ? "PASS" : "FAIL", okC ? "PASS" : "FAIL");

    // Each book is checked against ReferenceBook (src/reference_book.hpp), a
    // deliberately naive tick-array model, not against each other.
    std::printf("Lock-step workload comparison against an independent reference model (%zu ops;\n"
                "add/cancel result, BBO and 10-level bid/ask depth compared after every op):\n",
                w.ops.size());
    ReferenceBook ref;
    OrderBookA a; OrderBookB b; OrderBookC c;
    size_t bad_A = 0, bad_B = 0, bad_C = 0, first_bad = 0;
    bool workload_ok = true;
    for (size_t i = 0; i < w.ops.size(); ++i) {
        const Op& op = w.ops[i];
        Expected e{true, {}, {}, {}};
        if (op.type == OpType::Add) e.ret = ref.add_order_ticks(op.id, op.side, op.tick, op.qty);
        else if (op.type == OpType::Cancel) e.ret = ref.cancel_order(op.id);
        // the generator promises valid adds and cancels of live orders only
        if ((op.type == OpType::Add || op.type == OpType::Cancel) && !e.ret) workload_ok = false;
        e.bbo = ref.get_bbo();
        e.bids = ref.get_bids(10);
        e.asks = ref.get_asks(10);
        const bool okA_i = step_matches(a, op, e), okB_i = step_matches(b, op, e), okC_i = step_matches(c, op, e);
        bad_A += !okA_i;
        bad_B += !okB_i;
        bad_C += !okC_i;
        if ((!okA_i || !okB_i || !okC_i) && first_bad == 0) first_bad = i + 1;
    }
    std::printf("  mismatching ops: A %zu, B %zu, C %zu\n", bad_A, bad_B, bad_C);
    if (first_bad) std::printf("  first mismatch at op #%zu\n", first_bad);
    if (!workload_ok) std::printf("  workload error: an add was invalid or a cancel targeted a dead order\n");
    okA = okA && bad_A == 0 && workload_ok;
    okB = okB && bad_B == 0 && workload_ok;
    okC = okC && bad_C == 0 && workload_ok;

    // independent cross-check: same checksum from the benchmark path
    const uint64_t cr = run_once<ReferenceBook>(w).checksum;
    const uint64_t ca = run_once<OrderBookA>(w).checksum;
    const uint64_t cb = run_once<OrderBookB>(w).checksum;
    const uint64_t cc = run_once<OrderBookC>(w).checksum;
    std::printf("Benchmark checksums: reference=%llu A=%llu B=%llu C=%llu\n\n", (unsigned long long)cr,
                (unsigned long long)ca, (unsigned long long)cb, (unsigned long long)cc);
    okA = okA && ca == cr;
    okB = okB && cb == cr;
    okC = okC && cc == cr;

    std::printf("Implementation A: %s\n", okA ? "PASS" : "FAIL");
    std::printf("Implementation B: %s\n", okB ? "PASS" : "FAIL");
    std::printf("Implementation C: %s\n", okC ? "PASS" : "FAIL");
    const bool all = okA && okB && okC;
    std::printf("\n%s\n", all ? "All implementations produce identical results."
                              : "MISMATCH: implementations disagree.");
    return all ? 0 : 1;
}

// ----------------------------------------------------------------------------
static void usage() {
    std::printf("usage: bench --impl A|B|C [--runs N] [--ops N] [--seed S]\n"
                "       bench --impl A|B|C --perf        (single uninstrumented pass for perf)\n"
                "       bench --gen-only    [--ops N] [--seed S]   (generate the workload only)\n"
                "       bench --verify      [--ops N] [--seed S]\n");
}

// Strict unsigned parse for --runs/--ops/--seed: atoi/atoll silently turned
// "abc" into 0 and "-5" into a huge size_t that crashed in vector::reserve.
static unsigned long long parse_uint(const char* flag, const char* s, unsigned long long max) {
    char* end = nullptr;
    errno = 0;
    const unsigned long long v = std::strtoull(s, &end, 10);
    if (s[0] == '-' || end == s || *end != '\0' || errno == ERANGE || v > max) {
        std::fprintf(stderr, "%s expects an integer in [0, %llu], got '%s'\n", flag, max, s);
        std::exit(1);
    }
    return v;
}

int main(int argc, char** argv) {
    std::string impl;
    bool verify = false, perf = false, gen_only = false;
    int runs = 5;
    size_t ops = 1'000'000;
    uint32_t seed = 42;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", flag); std::exit(1); }
            return argv[++i];
        };
        if (a == "--impl") impl = next("--impl");
        else if (a == "--verify") verify = true;
        else if (a == "--perf") perf = true;
        else if (a == "--gen-only") gen_only = true;
        else if (a == "--runs") runs = static_cast<int>(parse_uint("--runs", next("--runs"), 1000));
        else if (a == "--ops") ops = static_cast<size_t>(parse_uint("--ops", next("--ops"), 100'000'000));
        else if (a == "--seed") seed = static_cast<uint32_t>(parse_uint("--seed", next("--seed"), 0xFFFFFFFFull));
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); usage(); return 1; }
    }
    if (!verify && !gen_only && impl.empty()) { usage(); return 1; }
    if (runs < 1) runs = 1;

    const long f0 = page_faults();
    const Workload w = make_workload(ops, seed);
    const long gen_faults = page_faults() - f0;

    if (verify) return run_verify(w);
    if (gen_only) {
        // Baseline for perf: `perf stat ./bench --gen-only` counts only what
        // every --perf run also pays for (workload generation, process start),
        // so subtracting it leaves the counters of the book itself.
        uint64_t h = 0;
        for (const Op& op : w.ops) h = mix(h, op.id ^ static_cast<uint64_t>(op.tick));
        std::printf("Workload only (--gen-only): %zu ops, %ld page faults during generation\n", w.ops.size(),
                    gen_faults);
        std::printf("Workload checksum: %llu\n", (unsigned long long)h);
        return 0;
    }
    if (impl == "A" || impl == "a")
        return perf ? run_perf<OrderBookA>("A", w, gen_faults) : run_bench<OrderBookA>("A", w, runs, gen_faults);
    if (impl == "B" || impl == "b")
        return perf ? run_perf<OrderBookB>("B", w, gen_faults) : run_bench<OrderBookB>("B", w, runs, gen_faults);
    if (impl == "C" || impl == "c")
        return perf ? run_perf<OrderBookC>("C", w, gen_faults) : run_bench<OrderBookC>("C", w, runs, gen_faults);
    std::fprintf(stderr, "unknown impl '%s' (expected A, B or C)\n", impl.c_str());
    return 1;
}
