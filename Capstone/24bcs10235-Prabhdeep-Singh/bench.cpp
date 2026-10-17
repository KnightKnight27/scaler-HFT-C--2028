#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

#include "impl_a.h"
#include "impl_b.h"
#include "impl_c.h"

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// Workload: 1M ops, seed 42, 70% add / 20% cancel / 10% BBO. Cancels always pick a live order.
// ---------------------------------------------------------------------------

enum class OpType { ADD, CANCEL, BBO };

struct Op {
    OpType   type;
    uint64_t id;
    Side     side;
    int      tick;  // price as integer tick
    long     qty;
};

static std::vector<Op> generate_workload() {
    const int NUM_OPS = 1'000'000;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int>  op_dist(0, 9);
    std::uniform_int_distribution<int>  bid_dist(MIN_BID_TICK, MAX_BID_TICK);   // 50.00..99.99
    std::uniform_int_distribution<int>  ask_dist(MIN_ASK_TICK, MAX_ASK_TICK);   // 100.01..150.00
    std::uniform_int_distribution<long> qty_dist(1, 100);
    std::uniform_int_distribution<int>  side_dist(0, 1);

    std::vector<Op>       ops;
    std::vector<uint64_t> active;   // ids of live orders
    ops.reserve(NUM_OPS);
    uint64_t next_id = 1;

    for (int i = 0; i < NUM_OPS; ++i) {
        int r = op_dist(rng);
        if (r <= 6 || active.empty()) {                           // 70% add
            Side side = side_dist(rng) ? Side::Buy : Side::Sell;
            int  tick = (side == Side::Buy) ? bid_dist(rng) : ask_dist(rng);
            ops.push_back({OpType::ADD, next_id, side, tick, qty_dist(rng)});
            active.push_back(next_id++);
        } else if (r <= 8) {                                      // 20% cancel
            std::uniform_int_distribution<size_t> pick(0, active.size() - 1);
            size_t pos = pick(rng);
            ops.push_back({OpType::CANCEL, active[pos], Side::Buy, 0, 0});
            active[pos] = active.back();
            active.pop_back();
        } else {                                                   // 10% BBO
            ops.push_back({OpType::BBO, 0, Side::Buy, 0, 0});
        }
    }
    return ops;
}

// ---------------------------------------------------------------------------
// Benchmark
// ---------------------------------------------------------------------------

struct RunResult {
    long total_ns = 0;   // wall clock for the whole workload, including the depth queries
    long add_ns = 0, cancel_ns = 0, bbo_ns = 0;
    long add_cnt = 0, cancel_cnt = 0, bbo_cnt = 0;
    long checksum = 0;
    BBO  final_bbo;
};

static long since(Clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
}

static long level_sum(const std::vector<Level>& v) {
    long s = 0;
    for (const auto& l : v) s += price_to_tick(l.price) + l.qty;
    return s;
}

template<typename Book>
static RunResult run_bench(const std::vector<Op>& ops) {
    Book      book;
    RunResult r;

    auto start = Clock::now();
    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];

        if (op.type == OpType::ADD) {
            double price = op.tick * 0.01;
            auto t0 = Clock::now();
            book.add_order(op.id, op.side, price, op.qty);
            r.add_ns += since(t0);
            ++r.add_cnt;

        } else if (op.type == OpType::CANCEL) {
            auto t0 = Clock::now();
            book.cancel_order(op.id);
            r.cancel_ns += since(t0);
            ++r.cancel_cnt;

        } else {
            auto t0 = Clock::now();
            BBO bbo = book.get_bbo();
            r.bbo_ns += since(t0);
            ++r.bbo_cnt;
            if (bbo.has_bid) r.checksum += price_to_tick(bbo.bid_price) + bbo.bid_qty;
            if (bbo.has_ask) r.checksum += price_to_tick(bbo.ask_price) + bbo.ask_qty;
        }

        if (i % 100 == 0) {
            r.checksum += level_sum(book.get_bids(10)) + level_sum(book.get_asks(10));
        }
    }
    r.total_ns  = since(start);
    r.final_bbo = book.get_bbo();   // printed below, so the work cannot be optimised away
    return r;
}

static void print_results(char impl, std::vector<RunResult>& runs) {
    std::sort(runs.begin(), runs.end(),
              [](const RunResult& a, const RunResult& b){ return a.total_ns < b.total_ns; });
    const RunResult& r = runs[runs.size() / 2];   // median run

    std::cout << "\n=== Implementation " << impl << " (median of " << runs.size() << " runs) ===\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total time:   " << r.total_ns / 1e6 << " ms\n";
    std::cout << "ns / add:     " << (double)r.add_ns    / r.add_cnt    << "\n";
    std::cout << "ns / cancel:  " << (double)r.cancel_ns / r.cancel_cnt << "\n";
    std::cout << "ns / bbo:     " << (double)r.bbo_ns    / r.bbo_cnt    << "\n";
    std::cout << "Final BBO:\n";
    if (r.final_bbo.has_bid) std::cout << "  Bid: " << r.final_bbo.bid_price << " x " << r.final_bbo.bid_qty << "\n";
    if (r.final_bbo.has_ask) std::cout << "  Ask: " << r.final_bbo.ask_price << " x " << r.final_bbo.ask_qty << "\n";
    std::cout << "Checksum:     " << r.checksum << "\n";
}

// ---------------------------------------------------------------------------
// Correctness
// ---------------------------------------------------------------------------

// The behaviours the assignment calls out, checked against explicit expected values.
template<typename Book>
static bool required_behaviour() {
    Book b;
    bool ok = true;
    auto expect = [&](bool cond) { ok &= cond; };

    BBO e = b.get_bbo();
    expect(!e.has_bid && !e.has_ask);                        // empty book
    expect(!b.cancel_order(42));                             // unknown id

    b.add_order(1, Side::Buy,  99.99,  10);
    b.add_order(2, Side::Buy,  99.99,   5);                  // same price: quantities add up
    b.add_order(3, Side::Buy,  99.98,   7);
    b.add_order(4, Side::Sell, 100.01,  3);
    BBO x = b.get_bbo();
    expect(x.has_bid && price_to_tick(x.bid_price) == 9999 && x.bid_qty == 15);
    expect(x.has_ask && price_to_tick(x.ask_price) == 10001 && x.ask_qty == 3);
    expect(b.get_bids(10).size() == 2 && b.get_asks(10).size() == 1);   // fewer levels than depth

    expect(b.cancel_order(1));
    expect(!b.cancel_order(1));                              // already cancelled
    expect(b.cancel_order(2) && b.cancel_order(3) && b.cancel_order(4));
    BBO z = b.get_bbo();
    expect(!z.has_bid && !z.has_ask);                        // empty again
    return ok;
}

static bool same(bool a, bool b) { return a == b; }
static bool same(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.has_ask == b.has_ask &&
           (!a.has_bid || (a.bid_price == b.bid_price && a.bid_qty == b.bid_qty)) &&
           (!a.has_ask || (a.ask_price == b.ask_price && a.ask_qty == b.ask_qty));
}
static bool same(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].price != b[i].price || a[i].qty != b[i].qty) return false;
    return true;
}

struct Verdict {
    bool ok[3]    = {true, true, true};   // A, B, C
    long checks   = 0;
    long failures = 0;
};

// Compare one result from A, B and C. The odd one out is blamed; if all three differ, all are.
template<typename T>
static void vote(Verdict& v, const T& a, const T& b, const T& c, const char* what, size_t op) {
    ++v.checks;
    bool ab = same(a, b), bc = same(b, c), ac = same(a, c);
    if (ab && bc) return;
    if (++v.failures <= 5) std::cout << "MISMATCH " << what << " at op " << op << "\n";
    if      (ab) v.ok[2] = false;
    else if (bc) v.ok[0] = false;
    else if (ac) v.ok[1] = false;
    else         v.ok[0] = v.ok[1] = v.ok[2] = false;
}

static bool verify(const std::vector<Op>& ops) {
    std::cout << "Running correctness test...\n\n";

    bool behaves[3] = {required_behaviour<OrderBookA>(), required_behaviour<OrderBookB>(),
                       required_behaviour<OrderBookC>()};

    OrderBookA a;
    OrderBookB b;
    OrderBookC c;
    Verdict v;

    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];

        if (op.type == OpType::ADD) {
            double p = op.tick * 0.01;
            a.add_order(op.id, op.side, p, op.qty);
            b.add_order(op.id, op.side, p, op.qty);
            c.add_order(op.id, op.side, p, op.qty);
        } else if (op.type == OpType::CANCEL) {
            vote(v, a.cancel_order(op.id), b.cancel_order(op.id), c.cancel_order(op.id), "cancel result", i);
        } else {
            vote(v, a.get_bbo(), b.get_bbo(), c.get_bbo(), "BBO", i);
        }

        if (i % 100 == 0) {
            vote(v, a.get_bids(10), b.get_bids(10), c.get_bids(10), "bid levels", i);
            vote(v, a.get_asks(10), b.get_asks(10), c.get_asks(10), "ask levels", i);
        }
    }
    vote(v, a.cancel_order(~0ULL), b.cancel_order(~0ULL), c.cancel_order(~0ULL), "unknown id cancel", ops.size());
    std::cout << v.checks << " comparisons on " << ops.size() << " random operations, "
              << v.failures << " mismatch(es)\n\n";

    bool all = true;
    for (int k = 0; k < 3; ++k) {
        bool pass = v.ok[k] && behaves[k];
        all &= pass;
        std::cout << "Implementation " << "ABC"[k] << ": " << (pass ? "PASS" : "FAIL") << "\n";
    }
    std::cout << "\n" << (all ? "All implementations produce identical results.\n"
                              : "Implementations DISAGREE.\n");
    return all;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    bool do_verify = false;
    char impl = 0;

    for (int i = 1; i < argc; ++i) {
        if      (strcmp(argv[i], "--verify") == 0) do_verify = true;
        else if (strcmp(argv[i], "--impl") == 0 && i + 1 < argc) impl = argv[++i][0];
    }

    if (!do_verify && impl != 'A' && impl != 'B' && impl != 'C') {
        std::cerr << "Usage: " << argv[0] << " --impl [A|B|C]\n"
                  << "       " << argv[0] << " --verify\n";
        return 1;
    }

    auto ops = generate_workload();
    if (do_verify) return verify(ops) ? 0 : 1;

    const int RUNS = 5;
    std::vector<RunResult> results;
    for (int r = 0; r < RUNS; ++r) {
        if      (impl == 'A') results.push_back(run_bench<OrderBookA>(ops));
        else if (impl == 'B') results.push_back(run_bench<OrderBookB>(ops));
        else                  results.push_back(run_bench<OrderBookC>(ops));
    }
    print_results(impl, results);
    return 0;
}
