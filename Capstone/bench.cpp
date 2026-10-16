// Rohan Ranjan-10428-Capstone project
//
// one program for everything:
//   ./bench --verify            correctness test, all three books vs a simple reference
//   ./bench --impl A|B|C        benchmark one book (5 runs, prints the median)
//   ./bench --impl none         only builds the workload (baseline for perf)
//
// extra flags: --runs N (default 5), --ops N (default 1000000), --seed N (default 42),
//              --total-only (skip the per-op timers, used under perf)
//
// build: g++ -std=c++20 -O2 bench.cpp -o bench

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

static inline long long ns_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
}

// ---------------------------------------------------------------------------
// checksum: mixes every result we get back, so the compiler can't drop any work.
// all three books must end with the same checksum.
// ---------------------------------------------------------------------------
struct Checksum {
    uint64_t v = 1469598103934665603ULL;
    void add(uint64_t x) { v = (v ^ x) * 1099511628211ULL; }
    void add(const BBO& b) {
        add(b.has_bid); add((uint64_t)std::llround(b.bid_price * 100)); add((uint64_t)b.bid_qty);
        add(b.has_ask); add((uint64_t)std::llround(b.ask_price * 100)); add((uint64_t)b.ask_qty);
    }
    void add(const std::vector<Level>& ls) {
        add(ls.size());
        for (auto& l : ls) { add((uint64_t)std::llround(l.price * 100)); add((uint64_t)l.qty); }
    }
};

// ---------------------------------------------------------------------------
// benchmark
// ---------------------------------------------------------------------------
// p50 / p99 / p99.9 / max of the single-call times, to see the spikes the average hides
struct Pcts {
    double p50 = 0, p99 = 0, p999 = 0, max = 0;
};

static Pcts percentiles(std::vector<long long>& v) {
    Pcts p;
    if (v.empty()) return p;
    std::sort(v.begin(), v.end());
    auto at = [&](double q) { return (double)v[std::min(v.size() - 1, (size_t)(q * v.size()))]; };
    p.p50 = at(0.50); p.p99 = at(0.99); p.p999 = at(0.999); p.max = (double)v.back();
    return p;
}

struct RunResult {
    double total_ms;
    double ns_add, ns_cancel, ns_bbo, ns_depth;
    Pcts   p_add, p_cancel, p_bbo;
    BBO    final_bbo;
};

// pass 1: whole workload, one timer around everything -> total time
template <typename Book>
double run_total(const std::vector<Op>& ops, Checksum& cs, BBO& final_bbo) {
    Book book;
    auto t0 = Clock::now();
    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        switch (op.type) {
            case OpType::Add:    book.add_order(op.id, op.side, op.price, op.qty); break;
            case OpType::Cancel: cs.add(book.cancel_order(op.id)); break;
            case OpType::Bbo:    cs.add(book.get_bbo()); break;
        }
        if ((i + 1) % 100 == 0) { cs.add(book.get_bids(10)); cs.add(book.get_asks(10)); }
    }
    auto t1 = Clock::now();
    final_bbo = book.get_bbo();
    return ns_between(t0, t1) / 1e6;
}

// pass 2: same workload, but a timer around every single call -> ns per op type
template <typename Book>
void run_per_op(const std::vector<Op>& ops, RunResult& r) {
    Book book;
    long long t_add = 0, t_cancel = 0, t_bbo = 0, t_depth = 0;
    long long n_add = 0, n_cancel = 0, n_bbo = 0, n_depth = 0;
    std::vector<long long> l_add, l_cancel, l_bbo; // every single call time
    l_add.reserve(ops.size()); l_cancel.reserve(ops.size()); l_bbo.reserve(ops.size());
    Checksum cs;
    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        auto t0 = Clock::now();
        switch (op.type) {
            case OpType::Add: {
                book.add_order(op.id, op.side, op.price, op.qty);
                long long d = ns_between(t0, Clock::now());
                t_add += d; n_add++; l_add.push_back(d);
                break;
            }
            case OpType::Cancel: {
                bool ok = book.cancel_order(op.id);
                long long d = ns_between(t0, Clock::now());
                t_cancel += d; n_cancel++; l_cancel.push_back(d);
                cs.add(ok);
                break;
            }
            case OpType::Bbo: {
                BBO b = book.get_bbo();
                long long d = ns_between(t0, Clock::now());
                t_bbo += d; n_bbo++; l_bbo.push_back(d);
                cs.add(b);
                break;
            }
        }
        if ((i + 1) % 100 == 0) {
            auto d0 = Clock::now();
            auto bids = book.get_bids(10);
            auto asks = book.get_asks(10);
            t_depth += ns_between(d0, Clock::now()); n_depth++;
            cs.add(bids); cs.add(asks);
        }
    }
    r.ns_add    = n_add    ? (double)t_add    / n_add    : 0;
    r.ns_cancel = n_cancel ? (double)t_cancel / n_cancel : 0;
    r.ns_bbo    = n_bbo    ? (double)t_bbo    / n_bbo    : 0;
    r.ns_depth  = n_depth  ? (double)t_depth  / n_depth  : 0;
    r.p_add     = percentiles(l_add);
    r.p_cancel  = percentiles(l_cancel);
    r.p_bbo     = percentiles(l_bbo);
    if (cs.v == 42) std::puts(""); // use the checksum so this pass isn't optimized away
}

// get_bbo is ~1 ns, smaller than the timer itself (and the Apple M4 steady_clock only
// ticks every ~42 ns). so it is also measured as 10M calls in a row on the final book.
// the empty asm tells the compiler the book may have changed, so it can't call get_bbo once
// and reuse the answer.
template <typename Book>
double bbo_loop_ns(const std::vector<Op>& ops) {
    Book book;
    for (const Op& op : ops) {
        if (op.type == OpType::Add)         book.add_order(op.id, op.side, op.price, op.qty);
        else if (op.type == OpType::Cancel) book.cancel_order(op.id);
    }
    const int N = 10000000;
    long sink = 0;
    auto t0 = Clock::now();
    for (int i = 0; i < N; ++i) {
        asm volatile("" : : "r"(&book) : "memory");
        BBO b = book.get_bbo();
        sink += b.bid_qty + b.ask_qty;
    }
    auto t1 = Clock::now();
    if (sink == 42) std::puts("");
    return (double)ns_between(t0, t1) / N;
}

// how long one Clock::now() pair takes, so we know how much of ns/op is the timer itself
static double timer_overhead_ns() {
    const int N = 1000000;
    long long sum = 0;
    for (int i = 0; i < N; ++i) {
        auto a = Clock::now();
        auto b = Clock::now();
        sum += ns_between(a, b);
    }
    return (double)sum / N;
}

static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

static void print_bbo(const BBO& b) {
    std::printf("Final BBO:\n");
    if (b.has_bid) std::printf("  Bid: %.2f x %ld\n", b.bid_price, b.bid_qty);
    else           std::printf("  Bid: none\n");
    if (b.has_ask) std::printf("  Ask: %.2f x %ld\n", b.ask_price, b.ask_qty);
    else           std::printf("  Ask: none\n");
}

template <typename Book>
int bench(const std::vector<Op>& ops, int runs, bool total_only) {
    std::printf("Implementation %s\n", Book::name());
    std::printf("%zu operations, %d runs, reporting the median\n\n", ops.size(), runs);

    std::vector<double> total, add, cancel, bbo, depth;
    std::vector<Pcts> p_add, p_cancel, p_bbo;
    uint64_t checksum = 0;
    BBO final_bbo;
    for (int r = 0; r < runs; ++r) {
        RunResult rr{};
        Checksum cs;
        rr.total_ms = run_total<Book>(ops, cs, rr.final_bbo);
        if (!total_only) run_per_op<Book>(ops, rr);
        total.push_back(rr.total_ms);
        add.push_back(rr.ns_add);
        cancel.push_back(rr.ns_cancel);
        bbo.push_back(rr.ns_bbo);
        depth.push_back(rr.ns_depth);
        p_add.push_back(rr.p_add);
        p_cancel.push_back(rr.p_cancel);
        p_bbo.push_back(rr.p_bbo);
        checksum  = cs.v;
        final_bbo = rr.final_bbo;
        if (runs > 1) std::printf("  run %d: total %.2f ms\n", r + 1, rr.total_ms);
    }

    std::printf("\nTotal time        : %8.2f ms\n", median(total));
    if (!total_only) {
        double oh = timer_overhead_ns();
        std::printf("Timer overhead    : %8.2f ns (one Clock::now() pair, included below)\n", oh);
        std::printf("ns / add_order    : %8.2f   (minus timer: %.2f)\n", median(add), median(add) - oh);
        std::printf("ns / cancel_order : %8.2f   (minus timer: %.2f)\n", median(cancel), median(cancel) - oh);
        std::printf("ns / get_bbo      : %8.2f   (minus timer: %.2f)\n", median(bbo), median(bbo) - oh);
        std::vector<double> loop;
        for (int r = 0; r < runs; ++r) loop.push_back(bbo_loop_ns<Book>(ops));
        std::printf("ns / get_bbo      : %8.2f   (10M calls in a tight loop, no timer per call)\n", median(loop));
        std::printf("ns / get_bids(10)+get_asks(10) : %8.2f\n", median(depth));

        // median over runs of each percentile (includes the timer, steps of ~42 ns on Apple M4)
        auto row = [&](const char* name, const std::vector<Pcts>& ps) {
            std::vector<double> a, b, c, d;
            for (auto& p : ps) { a.push_back(p.p50); b.push_back(p.p99); c.push_back(p.p999); d.push_back(p.max); }
            std::printf("  %-13s %8.0f %8.0f %8.0f %10.0f\n", name, median(a), median(b), median(c), median(d));
        };
        std::printf("\nsingle-call latency in ns (timer included):\n");
        std::printf("  %-13s %8s %8s %8s %10s\n", "", "p50", "p99", "p99.9", "max");
        row("add_order", p_add);
        row("cancel_order", p_cancel);
        row("get_bbo", p_bbo);
    }
    std::printf("\n");
    print_bbo(final_bbo);
    std::printf("Checksum: %llu\n", (unsigned long long)checksum);
    return 0;
}

// ---------------------------------------------------------------------------
// correctness test
// ---------------------------------------------------------------------------

// reference book: as simple as possible so it's easy to trust.
// qty per tick in an array, best price found by scanning ticks one by one.
class RefBook {
    struct OrderInfo { int tick; long qty; };
    std::unordered_map<uint64_t, OrderInfo> orders_;
    std::vector<long> qty_ = std::vector<long>(kNumTicks, 0);
    long& at(int tick) { return qty_[tick - kMinBidTick]; }
    long  at(int tick) const { return qty_[tick - kMinBidTick]; }

public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick;
        if (!parse_order(side, price, qty, tick)) return;
        if (orders_.count(id)) return;
        orders_[id] = {tick, qty};
        at(tick) += qty;
    }
    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        at(it->second.tick) -= it->second.qty;
        orders_.erase(it);
        return true;
    }
    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (int t = kMaxBidTick; t >= kMinBidTick && (int)out.size() < depth; --t)
            if (at(t) > 0) out.push_back({to_price(t), at(t)});
        return out;
    }
    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (int t = kMinAskTick; t <= kMaxAskTick && (int)out.size() < depth; ++t)
            if (at(t) > 0) out.push_back({to_price(t), at(t)});
        return out;
    }
    BBO get_bbo() const {
        BBO b;
        auto bids = get_bids(1), asks = get_asks(1);
        if (!bids.empty()) { b.has_bid = true; b.bid_price = bids[0].price; b.bid_qty = bids[0].qty; }
        if (!asks.empty()) { b.has_ask = true; b.ask_price = asks[0].price; b.ask_qty = asks[0].qty; }
        return b;
    }
};

static bool same(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.bid_price == b.bid_price && a.bid_qty == b.bid_qty &&
           a.has_ask == b.has_ask && a.ask_price == b.ask_price && a.ask_qty == b.ask_qty;
}

static bool same(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].price != b[i].price || a[i].qty != b[i].qty) return false;
    return true;
}

// tracks pass / fail for one book and remembers the first problem
struct Verdict {
    bool ok = true;
    std::string why;
    void check(bool cond, const std::string& what) {
        if (!cond && ok) { ok = false; why = what; }
    }
};

// hand-written edge cases from the assignment
template <typename Book>
void edge_cases(Verdict& v) {
    {
        Book b; // empty book
        BBO e = b.get_bbo();
        v.check(!e.has_bid && !e.has_ask, "empty book should have no bid and no ask");
        v.check(b.get_bids(10).empty() && b.get_asks(10).empty(), "empty book should have no levels");
        v.check(!b.cancel_order(12345), "cancel of unknown id should return false");
    }
    {
        Book b; // insufficient depth + same price adds up
        b.add_order(1, Side::Buy, 99.50, 10);
        b.add_order(2, Side::Buy, 99.50, 15);
        b.add_order(3, Side::Buy, 98.00, 7);
        b.add_order(4, Side::Buy, 99.99, 1);
        auto bids = b.get_bids(10);
        v.check(bids.size() == 3, "3 levels in book, get_bids(10) should return 3");
        v.check(same(bids, {{99.99, 1}, {99.50, 25}, {98.00, 7}}), "bid levels wrong (order or qty)");
        v.check(b.cancel_order(4), "cancel of live order should return true");
        v.check(!b.cancel_order(4), "second cancel of same id should return false");
        BBO x = b.get_bbo();
        v.check(x.has_bid && x.bid_price == 99.50 && x.bid_qty == 25, "best bid should fall to 99.50 x 25");
        v.check(!x.has_ask, "no asks were added");
        b.cancel_order(1);
        v.check(b.get_bbo().bid_qty == 15, "partial level cancel should leave 15");
    }
    {
        Book b; // asks go low -> high, best ask moves up
        b.add_order(10, Side::Sell, 101.00, 5);
        b.add_order(11, Side::Sell, 100.01, 3);
        b.add_order(12, Side::Sell, 150.00, 9);
        v.check(same(b.get_asks(2), {{100.01, 3}, {101.00, 5}}), "ask levels wrong");
        b.cancel_order(11);
        b.cancel_order(10);
        BBO x = b.get_bbo();
        v.check(x.has_ask && x.ask_price == 150.00 && x.ask_qty == 9, "best ask should move up to 150.00");
        b.cancel_order(12);
        v.check(!b.get_bbo().has_ask, "book should be empty again");
        b.add_order(13, Side::Sell, 120.00, 4); // level used again after being emptied
        v.check(b.get_bbo().ask_price == 120.00, "re-adding after empty should work");
    }
    {
        Book b; // best moves to the very next tick (no gap), and across a 64-tick boundary
        b.add_order(20, Side::Sell, 100.01, 1);
        b.add_order(21, Side::Sell, 100.02, 2);
        b.add_order(22, Side::Buy, 99.99, 3);
        b.add_order(23, Side::Buy, 99.98, 4);
        b.cancel_order(20);
        b.cancel_order(22);
        BBO x = b.get_bbo();
        v.check(x.ask_price == 100.02 && x.ask_qty == 2, "best ask should move to the next tick 100.02");
        v.check(x.bid_price == 99.98 && x.bid_qty == 4, "best bid should move to the next tick 99.98");

        Book c;
        for (int t = 9930; t <= 9999; ++t) c.add_order(1000 + t, Side::Buy, to_price(t), t);
        for (int t = 9999; t > 9930; --t) {
            c.cancel_order(1000 + t);
            v.check(c.get_bbo().bid_price == to_price(t - 1), "best bid wrong while walking down tick by tick");
        }
        for (int t = 10001; t <= 10070; ++t) c.add_order(2000 + t, Side::Sell, to_price(t), t);
        for (int t = 10001; t < 10070; ++t) {
            c.cancel_order(2000 + t);
            v.check(c.get_bbo().ask_price == to_price(t + 1), "best ask wrong while walking up tick by tick");
        }
        v.check(c.get_bids(3).size() == 1 && c.get_asks(3).size() == 1, "one level each side should be left");
    }
    {
        Book b; // bad orders are ignored, duplicate live id is ignored
        b.add_order(1, Side::Buy, 100.00, 5);   // mid price, not a valid bid
        b.add_order(2, Side::Buy, 100.01, 5);   // ask range
        b.add_order(3, Side::Sell, 99.99, 5);   // bid range
        b.add_order(4, Side::Buy, 49.99, 5);    // below range
        b.add_order(5, Side::Sell, 150.01, 5);  // above range
        b.add_order(6, Side::Buy, 99.995, 5);   // not a multiple of 0.01
        b.add_order(7, Side::Buy, 99.00, 0);    // zero qty
        BBO x = b.get_bbo();
        v.check(!x.has_bid && !x.has_ask, "invalid orders should be ignored");
        v.check(!b.cancel_order(1), "ignored order should not be cancellable");
        b.add_order(8, Side::Buy, 99.00, 5);
        b.add_order(8, Side::Buy, 98.00, 5);    // same id again while live
        v.check(same(b.get_bids(10), {{99.00, 5}}), "duplicate live id should be ignored");
        b.add_order(100000000ULL, Side::Buy, 99.10, 2); // very large id
        v.check(b.get_bbo().bid_price == 99.10, "large id should work");
        v.check(b.cancel_order(100000000ULL), "large id cancel should work");
    }
}

// random workload: after every op compare BBO + top 10 levels with the reference,
// and every 1000 ops compare the whole book
template <typename Book>
void random_check(const std::vector<Op>& ops, Verdict& v) {
    Book    b;
    RefBook ref;
    for (size_t i = 0; i < ops.size() && v.ok; ++i) {
        const Op& op = ops[i];
        if (op.type == OpType::Add) {
            b.add_order(op.id, op.side, op.price, op.qty);
            ref.add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == OpType::Cancel) {
            v.check(b.cancel_order(op.id) == ref.cancel_order(op.id), "cancel result differs at op " + std::to_string(i));
            v.check(!b.cancel_order(op.id), "double cancel returned true at op " + std::to_string(i));
            ref.cancel_order(op.id);
        }
        v.check(same(b.get_bbo(), ref.get_bbo()), "BBO differs at op " + std::to_string(i));
        v.check(same(b.get_bids(10), ref.get_bids(10)), "bid levels differ at op " + std::to_string(i));
        v.check(same(b.get_asks(10), ref.get_asks(10)), "ask levels differ at op " + std::to_string(i));
        if (i % 1000 == 0) {
            v.check(same(b.get_bids(kNumTicks), ref.get_bids(kNumTicks)), "full bid book differs at op " + std::to_string(i));
            v.check(same(b.get_asks(kNumTicks), ref.get_asks(kNumTicks)), "full ask book differs at op " + std::to_string(i));
        }
    }
    v.check(same(b.get_bids(kNumTicks), ref.get_bids(kNumTicks)), "final bid book differs");
    v.check(same(b.get_asks(kNumTicks), ref.get_asks(kNumTicks)), "final ask book differs");
}

template <typename Book>
bool verify_one(const char* letter, const std::vector<std::vector<Op>>& workloads) {
    Verdict v;
    edge_cases<Book>(v);
    for (auto& ops : workloads) random_check<Book>(ops, v);
    if (v.ok) std::printf("Implementation %s: PASS\n", letter);
    else      std::printf("Implementation %s: FAIL (%s)\n", letter, v.why.c_str());
    return v.ok;
}

static int verify(const std::vector<Op>& ops, uint32_t seed) {
    // 1) the benchmark workload itself
    // 2) two thin books: 50% add / 40% cancel, prices only near the middle,
    //    so the best bid/ask empties and moves all the time
    std::vector<std::vector<Op>> workloads = {
        ops,
        make_workload(ops.size() / 2, seed + 1, 50, 40, 8),
        make_workload(ops.size() / 2, seed + 2, 50, 40, 200),
    };
    std::printf("Running correctness test...\n");
    std::printf("(edge cases + benchmark workload (%zu ops) + 2 thin-book workloads (%zu ops each).\n",
                ops.size(), ops.size() / 2);
    std::printf(" BBO and top 10 levels checked after every op, full book every 1000 ops,\n");
    std::printf(" all compared against a simple reference book)\n\n");
    bool a = verify_one<BookA>("A", workloads);
    bool b = verify_one<BookB>("B", workloads);
    bool c = verify_one<BookC>("C", workloads);
    std::printf("\n");
    if (a && b && c) {
        std::printf("All implementations produce identical results.\n");
        return 0;
    }
    std::printf("Implementations DO NOT agree.\n");
    return 1;
}

// ---------------------------------------------------------------------------

static void usage() {
    std::printf("usage: ./bench --impl A|B|C|none [--runs N] [--ops N] [--seed N] [--total-only]\n");
    std::printf("       ./bench --verify [--ops N] [--seed N]\n");
}

int main(int argc, char** argv) {
    std::string impl;
    bool do_verify = false, total_only = false;
    int runs = 5;
    size_t n_ops = 1000000;
    uint32_t seed = 42;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) { usage(); std::exit(2); }
            return argv[++i];
        };
        if      (a == "--impl")       impl = next();
        else if (a == "--verify")     do_verify = true;
        else if (a == "--runs")       runs = std::max(1, std::atoi(next()));
        else if (a == "--ops")        n_ops = std::strtoull(next(), nullptr, 10);
        else if (a == "--seed")       seed = (uint32_t)std::strtoul(next(), nullptr, 10);
        else if (a == "--total-only") total_only = true;
        else { usage(); return 2; }
    }

    std::vector<Op> ops = make_workload(n_ops, seed);

    if (do_verify) return verify(ops, seed);
    if (impl == "A" || impl == "a") return bench<BookA>(ops, runs, total_only);
    if (impl == "B" || impl == "b") return bench<BookB>(ops, runs, total_only);
    if (impl == "C" || impl == "c") return bench<BookC>(ops, runs, total_only);
    if (impl == "none") { std::printf("workload only: %zu ops\n", ops.size()); return 0; }
    usage();
    return 2;
}
