// One program for everything:
//   ./bench --verify                  correctness test: A, B, C must agree (plus edge cases)
//   ./bench --impl A|B|C|all          benchmark, 5 runs each, median reported
//   ./bench --impl A --runs 1 --total-only   one untimed-per-op pass (use this under perf)
//   ./bench --impl none --total-only  only generates the workload (perf baseline)
// Options: --runs N  --ops N  --seed N

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "book_a.hpp"
#include "book_b.hpp"
#include "book_c.hpp"
#include "workload.hpp"

using Clock = std::chrono::steady_clock;

// Everything the book returns is folded into this, and it is printed at the end,
// so the compiler cannot drop any of the work.
struct Checksum {
    uint64_t v = 1469598103934665603ull;
    void mix(uint64_t x) { v = (v ^ x) * 1099511628211ull; }
    void mix(const BBO& b) {
        mix(b.has_bid ? (uint64_t)ticks::from_price(b.bid_price) * 1000003 + b.bid_qty : 7);
        mix(b.has_ask ? (uint64_t)ticks::from_price(b.ask_price) * 1000003 + b.ask_qty : 11);
    }
    void mix(const std::vector<Level>& lv) {
        mix(lv.size());
        for (const Level& l : lv) mix((uint64_t)ticks::from_price(l.price) * 1000003 + l.qty);
    }
};

template <class Book>
inline void apply(Book& book, const Op& op, Checksum& cs) {
    switch (op.type) {
        case OpType::Add: book.add_order(op.id, op.side, op.price, op.qty); break;
        case OpType::Cancel: cs.mix(book.cancel_order(op.id)); break;
        case OpType::Bbo: cs.mix(book.get_bbo()); break;
        case OpType::Depth:
            cs.mix(book.get_bids(10));
            cs.mix(book.get_asks(10));
            break;
    }
}

// ---------------------------------------------------------------- benchmark

struct RunResult {
    double total_ms = 0;  // whole workload, no per-op timers
    double ns[4] = {};    // per-op average by OpType (gross, includes one clock pair)
    uint64_t checksum = 0;
    BBO final_bbo;
};

struct ClockInfo {
    double pair_ns;        // average cost of two back-to-back now() calls
    double resolution_ns;  // smallest non-zero step the clock ever reports
};

// Per-op timings are only as fine as the clock. On Apple Silicon steady_clock ticks
// every ~41.7 ns (24 MHz), so a single fast op reads as 0 or 41.7 ns and only the
// average over many ops is meaningful.
static ClockInfo clock_info() {
    const int n = 1'000'000;
    long long sink = 0;
    double res = 1e9;
    auto t0 = Clock::now();
    for (int i = 0; i < n; ++i) {
        auto a = Clock::now();
        auto b = Clock::now();
        const double d = std::chrono::duration<double, std::nano>(b - a).count();
        if (d > 0 && d < res) res = d;
        sink += (b - a).count();
    }
    auto t1 = Clock::now();
    if (sink == -1) std::puts("");
    return {std::chrono::duration<double, std::nano>(t1 - t0).count() / n, res};
}

// get_bbo is cheaper than the clock, so it is also timed as one block of calls on the
// final book of the workload: (block time) / calls, no per-call timer.
template <class Book>
double bbo_block_ns(const std::vector<Op>& ops) {
    Book book;
    Checksum cs;
    for (const Op& op : ops) apply(book, op, cs);
    const int n = 1'000'000;
    auto t0 = Clock::now();
    for (int i = 0; i < n; ++i) cs.mix(book.get_bbo());
    auto t1 = Clock::now();
    if (cs.v == 42) std::puts("");
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / n;
}

template <class Book>
RunResult run_once(const std::vector<Op>& ops, bool total_only) {
    RunResult r;
    {  // pass 1: total time, nothing but the book calls inside the loop
        Book book;
        Checksum cs;
        auto t0 = Clock::now();
        for (const Op& op : ops) apply(book, op, cs);
        auto t1 = Clock::now();
        r.total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        r.checksum = cs.v;
        r.final_bbo = book.get_bbo();
    }
    if (total_only) return r;

    {  // pass 2: same workload on a fresh book, every op wrapped in its own timer
        Book book;
        Checksum cs;
        double sum[4] = {};
        size_t cnt[4] = {};
        for (const Op& op : ops) {
            auto t0 = Clock::now();
            apply(book, op, cs);
            auto t1 = Clock::now();
            const int k = (int)op.type;
            sum[k] += std::chrono::duration<double, std::nano>(t1 - t0).count();
            ++cnt[k];
        }
        for (int k = 0; k < 4; ++k) r.ns[k] = cnt[k] ? sum[k] / cnt[k] : 0;
        if (cs.v != r.checksum) {
            std::fprintf(stderr, "checksum mismatch between passes\n");
            std::exit(2);
        }
    }
    return r;
}

static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

template <class Book>
void bench(const char* name, const std::vector<Op>& ops, int runs, bool total_only) {
    std::vector<double> total, add, cancel, bbo, depth, bbo_block;
    uint64_t checksum = 0;
    BBO b;
    // Warm-up pass, not reported. Skipped in --total-only so that under perf the
    // process does exactly `runs` passes over the workload.
    if (!total_only) run_once<Book>(ops, true);
    for (int i = 0; i < runs; ++i) {
        RunResult r = run_once<Book>(ops, total_only);
        total.push_back(r.total_ms);
        add.push_back(r.ns[(int)OpType::Add]);
        cancel.push_back(r.ns[(int)OpType::Cancel]);
        bbo.push_back(r.ns[(int)OpType::Bbo]);
        depth.push_back(r.ns[(int)OpType::Depth]);
        if (!total_only) bbo_block.push_back(bbo_block_ns<Book>(ops));
        checksum = r.checksum;
        b = r.final_bbo;
    }

    std::printf("Implementation %s  (median of %d runs)\n", name, runs);
    std::printf("  total time            : %8.2f ms\n", median(total));
    if (!total_only) {
        std::printf("  ns / add_order        : %8.1f\n", median(add));
        std::printf("  ns / cancel_order     : %8.1f\n", median(cancel));
        std::printf("  ns / get_bbo          : %8.1f   (block of 1M calls: %.2f ns)\n", median(bbo), median(bbo_block));
        std::printf("  ns / get_bids+asks(10): %8.1f\n", median(depth));
    }

    // Printed so the compiler cannot treat the work as dead.
    std::printf("  final BBO             : bid %.2f x %ld | ask %.2f x %ld\n", b.bid_price, b.bid_qty, b.ask_price, b.ask_qty);
    std::printf("  checksum              : %016llx\n\n", (unsigned long long)checksum);
}

// ---------------------------------------------------------------- verify

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::printf("    FAILED: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        }                                                                        \
    } while (0)

// Required behaviours from the brief, checked on one implementation.
template <class Book>
bool edge_cases() {
    const int before = g_failures;
    {
        Book b;  // empty book
        BBO e = b.get_bbo();
        CHECK(!e.has_bid && !e.has_ask);
        CHECK(b.get_bids(10).empty() && b.get_asks(10).empty());
        CHECK(!b.cancel_order(12345));  // unknown id
    }
    {
        Book b;
        b.add_order(1, Side::Buy, 99.50, 10);
        b.add_order(2, Side::Buy, 99.50, 15);   // same price: quantities add up
        b.add_order(3, Side::Buy, 98.00, 5);
        b.add_order(4, Side::Buy, 50.00, 1);    // band edges
        b.add_order(5, Side::Sell, 100.01, 7);
        b.add_order(6, Side::Sell, 150.00, 3);
        BBO x = b.get_bbo();
        CHECK(x.has_bid && x.bid_price == 99.50 && x.bid_qty == 25);
        CHECK(x.has_ask && x.ask_price == 100.01 && x.ask_qty == 7);

        auto bids = b.get_bids(10);  // only 3 levels exist
        CHECK(bids.size() == 3);
        CHECK(bids.size() == 3 && bids[0] == (Level{99.50, 25}) && bids[1] == (Level{98.00, 5}) && bids[2] == (Level{50.00, 1}));
        CHECK(b.get_asks(1).size() == 1);
        CHECK(b.get_bids(0).empty());

        CHECK(b.cancel_order(1));
        CHECK(b.get_bbo().bid_qty == 15);       // level shrinks, stays
        CHECK(!b.cancel_order(1));              // already cancelled
        CHECK(b.cancel_order(2));
        CHECK(b.get_bbo().bid_price == 98.00);  // best level emptied -> next one
        CHECK(b.cancel_order(5));
        CHECK(b.get_bbo().ask_price == 150.00);
        CHECK(b.cancel_order(6));
        CHECK(!b.get_bbo().has_ask);            // ask side empty again

        // Invalid input is ignored, not stored.
        b.add_order(10, Side::Buy, 100.00, 5);   // mid price is in neither band
        b.add_order(11, Side::Sell, 99.99, 5);   // ask in the bid band
        b.add_order(12, Side::Buy, 49.99, 5);    // below range
        b.add_order(13, Side::Buy, 99.505, 5);   // not a multiple of 0.01
        b.add_order(14, Side::Buy, 99.00, 0);    // zero qty
        b.add_order(3, Side::Buy, 97.00, 5);     // duplicate id
        CHECK(b.get_bids(10).size() == 2);
        CHECK(!b.cancel_order(10) && !b.cancel_order(11) && !b.cancel_order(13));
        CHECK(b.get_bbo().bid_qty == 5);
    }
    return g_failures == before;
}

// Per-implementation failure counts. Every result is compared pairwise (A-B, A-C, B-C);
// an implementation is blamed when it disagrees with both of the others.
struct Blame {
    size_t a = 0, b = 0, c = 0;
    size_t total() const { return a + b + c; }
};

template <class T>
static bool same3(const T& ra, const T& rb, const T& rc, Blame& blame) {
    const bool ab = ra == rb, ac = ra == rc, bc = rb == rc;
    if (ab && ac) return true;
    if (!ab && !ac) ++blame.a;
    if (!ab && !bc) ++blame.b;
    if (!ac && !bc) ++blame.c;
    if (!ab && !ac && !bc) ++blame.a, ++blame.b, ++blame.c;  // all three differ
    return false;
}

// Runs one workload through A, B and C in lockstep and compares every result.
// depth_every_op: compare top-10 every N ops; full_every: compare every level every N ops.
static bool lockstep(const char* label, const WorkloadConfig& cfg, int depth_every_op, int full_every, Blame& blame) {
    std::vector<Op> ops = make_workload(cfg);
    BookA a;
    BookB b;
    BookC c;
    size_t mismatches = 0, compared_depth = 0;
    auto check = [&](bool ok, size_t i, const char* what) {
        if (!ok && ++mismatches <= 5) std::printf("    mismatch at op %zu: %s\n", i, what);
    };

    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        if (op.type == OpType::Add) {
            a.add_order(op.id, op.side, op.price, op.qty);
            b.add_order(op.id, op.side, op.price, op.qty);
            c.add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == OpType::Cancel) {
            // The workload only cancels live orders, so all three must return true.
            const bool ra = a.cancel_order(op.id), rb = b.cancel_order(op.id), rc = c.cancel_order(op.id);
            check(same3(ra, rb, rc, blame) && ra, i, "cancel return value");
            if (i % 97 == 0) {  // cancelling the same id again must fail everywhere
                const bool da = a.cancel_order(op.id), db = b.cancel_order(op.id), dc = c.cancel_order(op.id);
                check(same3(da, db, dc, blame) && !da, i, "double cancel return value");
            }
        }

        // BBO after every single op.
        check(same3(a.get_bbo(), b.get_bbo(), c.get_bbo(), blame), i, "BBO");

        // Top-10 levels (prices and quantities) on both sides.
        if (op.type == OpType::Depth || i % depth_every_op == 0) {
            check(same3(a.get_bids(10), b.get_bids(10), c.get_bids(10), blame), i, "top-10 bids");
            check(same3(a.get_asks(10), b.get_asks(10), c.get_asks(10), blame), i, "top-10 asks");
            ++compared_depth;
        }
        // Whole book, every level.
        if (i % full_every == 0 || i + 1 == ops.size()) {
            const int all = 1 << 20;
            check(same3(a.get_bids(all), b.get_bids(all), c.get_bids(all), blame), i, "all bid levels");
            check(same3(a.get_asks(all), b.get_asks(all), c.get_asks(all), blame), i, "all ask levels");
        }
    }
    std::printf("  %-34s %8zu ops, %8zu BBO + %7zu top-10 compares, %4zu bid levels left: %s\n", label, ops.size(),
                ops.size(), compared_depth, a.get_bids(1 << 20).size(), mismatches ? "MISMATCH" : "agree");
    return mismatches == 0;
}

static int verify() {
    std::printf("Running correctness test...\n\n");
    std::printf("Edge cases (empty book, unknown/double cancel, insufficient depth, invalid input):\n");
    const bool ea = edge_cases<BookA>();
    const bool eb = edge_cases<BookB>();
    const bool ec = edge_cases<BookC>();
    std::printf("  A: %s   B: %s   C: %s\n\n", ea ? "ok" : "FAIL", eb ? "ok" : "FAIL", ec ? "ok" : "FAIL");

    std::printf("Lockstep comparison of A, B and C on identical random workloads:\n");
    Blame blame;
    WorkloadConfig graded;  // exactly the benchmark workload: 1M ops, seed 42, 70/20/10
    bool ok = lockstep("benchmark workload (seed 42)", graded, 100, 50'000, blame);

    WorkloadConfig narrow;  // few levels, cancel-heavy: best levels empty constantly
    narrow.ops = 300'000;
    narrow.seed = 7;
    narrow.band_ticks = 12;
    narrow.add_pct = 50;
    narrow.cancel_pct = 45;
    ok &= lockstep("narrow band, cancel-heavy (seed 7)", narrow, 1, 1'000, blame);

    WorkloadConfig scrambled;  // non-sequential ids
    scrambled.ops = 300'000;
    scrambled.seed = 2026;
    scrambled.scramble_ids = true;
    ok &= lockstep("scrambled ids (seed 2026)", scrambled, 10, 10'000, blame);

    WorkloadConfig drain;  // cancel more than add: book repeatedly runs empty
    drain.ops = 200'000;
    drain.seed = 99;
    drain.band_ticks = 40;
    drain.add_pct = 40;
    drain.cancel_pct = 55;
    ok &= lockstep("drain to empty (seed 99)", drain, 1, 500, blame);

    const bool pa = ea && blame.a == 0, pb = eb && blame.b == 0, pc = ec && blame.c == 0;
    std::printf("\nImplementation A: %s\n", pa ? "PASS" : "FAIL");
    std::printf("Implementation B: %s\n", pb ? "PASS" : "FAIL");
    std::printf("Implementation C: %s\n\n", pc ? "PASS" : "FAIL");
    const bool all = ok && pa && pb && pc;
    if (all) std::printf("All implementations produce identical results.\n");
    else std::printf("Implementations DISAGREE (blamed: A %zu, B %zu, C %zu; edge-case failures %d).\n", blame.a, blame.b, blame.c, g_failures);
    return all ? 0 : 1;
}

// ---------------------------------------------------------------- main

int main(int argc, char** argv) {
    std::string impl = "all";
    int runs = 5;
    bool total_only = false;
    WorkloadConfig cfg;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value after %s\n", a.c_str());
                std::exit(1);
            }
            return argv[++i];
        };
        if (a == "--verify") return verify();
        else if (a == "--impl") impl = next();
        else if (a == "--runs") runs = std::max(1, std::atoi(next()));
        else if (a == "--ops") cfg.ops = std::strtoull(next(), nullptr, 10);
        else if (a == "--seed") cfg.seed = (uint32_t)std::strtoul(next(), nullptr, 10);
        else if (a == "--total-only") total_only = true;
        else {
            std::fprintf(stderr, "usage: %s [--verify] [--impl A|B|C|all|none] [--runs N] [--ops N] [--seed N] [--total-only]\n", argv[0]);
            return 1;
        }
    }

    std::vector<Op> ops = make_workload(cfg);
    size_t n[4] = {};
    for (const Op& op : ops) ++n[(int)op.type];
    if (impl == "none") {
        std::printf("generated %zu ops (workload only, no book)\n", ops.size());
        return 0;
    }

    const ClockInfo clk = total_only ? ClockInfo{0, 0} : clock_info();
    std::printf("Workload: %zu ops, seed %u -> %zu add, %zu cancel, %zu get_bbo, %zu get_bids+get_asks(10)\n", cfg.ops,
                cfg.seed, n[0], n[1], n[2], n[3]);
    if (!total_only)
        std::printf("steady_clock: resolution %.1f ns, now()+now() pair %.1f ns (included in every per-op number)\n",
                    clk.resolution_ns, clk.pair_ns);
    std::printf("\n");

    if (impl == "A" || impl == "all") bench<BookA>("A (std::map + unordered_map)", ops, runs, total_only);
    if (impl == "B" || impl == "all") bench<BookB>("B (heaps + lazy delete)", ops, runs, total_only);
    if (impl == "C" || impl == "all") bench<BookC>("C (tick array + bitmap + own hash)", ops, runs, total_only);
    if (impl != "A" && impl != "B" && impl != "C" && impl != "all") {
        std::fprintf(stderr, "unknown --impl %s\n", impl.c_str());
        return 1;
    }
    return 0;
}
