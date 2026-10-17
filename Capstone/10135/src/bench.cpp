// Single driver for all three order book builds.
//
//   ./bench --impl A|B|C     benchmark one build (1,000,000 ops, 5 runs, median)
//   ./bench --verify         replay one workload through A, B and C and check
//                            that they agree on every operation
//   ./bench --test           small hand-written edge-case checks
//   ./bench --ops N          override the operation count (quick experiments)
//   ./bench --runs N         override the number of timed runs
//
// The workload is generated once into a flat list and then replayed, so A, B and
// C all see exactly the same sequence of operations.
#include "common.hpp"
#include "book_map.hpp"
#include "book_heap.hpp"
#include "book_array.hpp"

#include <chrono>
#include <random>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <stdexcept>

namespace {

enum class OpType : uint8_t { Add, Cancel, Bbo };

struct Op {
    OpType type;
    uint64_t id;      // add + cancel
    Side side;        // add
    double price;     // add
    long qty;         // add
};

constexpr int kDepthEvery = 100;   // run depth snapshots every 100 ops
constexpr int kDepth = 10;

// Build the shared workload. Liveness is tracked independently of any book so
// the exact same list drives every implementation, and cancels always name an
// order that is currently resting.
std::vector<Op> build_workload(size_t n_ops) {
    std::vector<Op> ops;
    ops.reserve(n_ops);

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> pick(0, 99);
    std::uniform_int_distribution<int> qty_dist(1, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> bid_tick(ticks::kBidMin, ticks::kBidMax);
    std::uniform_int_distribution<int> ask_tick(ticks::kAskMin, ticks::kAskMax);

    std::vector<uint64_t> live;     // ids currently resting
    live.reserve(n_ops);
    uint64_t next_id = 1;

    for (size_t i = 0; i < n_ops; ++i) {
        int r = pick(rng);
        bool do_cancel = (r >= 70 && r < 90) && !live.empty();
        bool do_bbo = (r >= 90);

        if (do_bbo) {
            ops.push_back(Op{OpType::Bbo, 0, Side::Buy, 0.0, 0});
        } else if (do_cancel) {
            std::uniform_int_distribution<size_t> idx(0, live.size() - 1);
            size_t j = idx(rng);
            uint64_t id = live[j];
            live[j] = live.back();
            live.pop_back();
            ops.push_back(Op{OpType::Cancel, id, Side::Buy, 0.0, 0});
        } else {
            // add (also the fallback when a cancel was drawn but the book is empty)
            Side side = side_dist(rng) ? Side::Sell : Side::Buy;
            int tick = (side == Side::Buy) ? bid_tick(rng) : ask_tick(rng);
            long qty = qty_dist(rng);
            uint64_t id = next_id++;
            live.push_back(id);
            ops.push_back(Op{OpType::Add, id, side, tick_to_price(tick), qty});
        }
    }
    return ops;
}

// Fold a BBO and a set of levels into a running checksum so the optimiser cannot
// delete the query work, and so --verify has a single number to compare.
inline uint64_t fold_u64(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

uint64_t fold_bbo(uint64_t h, const BBO& b) {
    h = fold_u64(h, b.has_bid ? 1u : 0u);
    if (b.has_bid) {
        h = fold_u64(h, static_cast<uint64_t>(std::lround(b.bid_price * 100.0)));
        h = fold_u64(h, static_cast<uint64_t>(b.bid_qty));
    }
    h = fold_u64(h, b.has_ask ? 1u : 0u);
    if (b.has_ask) {
        h = fold_u64(h, static_cast<uint64_t>(std::lround(b.ask_price * 100.0)));
        h = fold_u64(h, static_cast<uint64_t>(b.ask_qty));
    }
    return h;
}

uint64_t fold_levels(uint64_t h, const std::vector<Level>& levels) {
    h = fold_u64(h, levels.size());
    for (const Level& l : levels) {
        h = fold_u64(h, static_cast<uint64_t>(std::lround(l.price * 100.0)));
        h = fold_u64(h, static_cast<uint64_t>(l.qty));
    }
    return h;
}

struct RunResult {
    double total_ms;
    double ns_add;
    double ns_cancel;
    double ns_bbo;
    uint64_t checksum;
    BBO final_bbo;
};

using Clock = std::chrono::steady_clock;

template <typename Book>
RunResult run_once(const std::vector<Op>& ops) {
    Book book;
    uint64_t checksum = 1469598103934665603ULL;

    long long add_ns = 0, cancel_ns = 0, bbo_ns = 0;
    long add_n = 0, cancel_n = 0, bbo_n = 0;

    auto wall_start = Clock::now();
    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        switch (op.type) {
            case OpType::Add: {
                auto t0 = Clock::now();
                book.add_order(op.id, op.side, op.price, op.qty);
                auto t1 = Clock::now();
                add_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
                ++add_n;
                break;
            }
            case OpType::Cancel: {
                auto t0 = Clock::now();
                bool ok = book.cancel_order(op.id);
                auto t1 = Clock::now();
                cancel_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
                ++cancel_n;
                checksum = fold_u64(checksum, ok ? 1u : 0u);
                break;
            }
            case OpType::Bbo: {
                auto t0 = Clock::now();
                BBO b = book.get_bbo();
                auto t1 = Clock::now();
                bbo_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
                ++bbo_n;
                checksum = fold_bbo(checksum, b);
                break;
            }
        }
        if ((i + 1) % kDepthEvery == 0) {
            checksum = fold_levels(checksum, book.get_bids(kDepth));
            checksum = fold_levels(checksum, book.get_asks(kDepth));
        }
    }
    auto wall_end = Clock::now();

    RunResult res;
    res.total_ms = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();
    res.ns_add = add_n ? static_cast<double>(add_ns) / add_n : 0.0;
    res.ns_cancel = cancel_n ? static_cast<double>(cancel_ns) / cancel_n : 0.0;
    res.ns_bbo = bbo_n ? static_cast<double>(bbo_ns) / bbo_n : 0.0;
    res.checksum = checksum;
    res.final_bbo = book.get_bbo();
    return res;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if (n == 0) return 0.0;
    return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

template <typename Book>
int benchmark(const std::vector<Op>& ops, int runs) {
    std::vector<RunResult> results;
    for (int r = 0; r < runs; ++r) {
        results.push_back(run_once<Book>(ops));
    }

    std::vector<double> t, a, c, b;
    for (const RunResult& r : results) {
        t.push_back(r.total_ms);
        a.push_back(r.ns_add);
        c.push_back(r.ns_cancel);
        b.push_back(r.ns_bbo);
    }

    const RunResult& last = results.back();
    std::printf("Implementation %s\n", Book().name());
    std::printf("Ops: %zu   Runs: %d (median reported)\n\n", ops.size(), runs);
    std::printf("  Total time : %10.2f ms\n", median(t));
    std::printf("  ns / add   : %10.2f\n", median(a));
    std::printf("  ns / cancel: %10.2f\n", median(c));
    std::printf("  ns / bbo   : %10.2f\n\n", median(b));

    std::printf("Final BBO:\n");
    if (last.final_bbo.has_bid)
        std::printf("  Bid: %.2f x %ld\n", last.final_bbo.bid_price, last.final_bbo.bid_qty);
    else
        std::printf("  Bid: (none)\n");
    if (last.final_bbo.has_ask)
        std::printf("  Ask: %.2f x %ld\n", last.final_bbo.ask_price, last.final_bbo.ask_qty);
    else
        std::printf("  Ask: (none)\n");
    std::printf("\nChecksum: %llu\n", static_cast<unsigned long long>(last.checksum));
    return 0;
}

// ---- verification -------------------------------------------------------------
// Replay the same workload through all three books in lock step and compare the
// full visible state after every operation.

bool bbo_equal(const BBO& x, const BBO& y) {
    return x.has_bid == y.has_bid && x.has_ask == y.has_ask &&
           (!x.has_bid || (x.bid_price == y.bid_price && x.bid_qty == y.bid_qty)) &&
           (!x.has_ask || (x.ask_price == y.ask_price && x.ask_qty == y.ask_qty));
}

bool levels_equal(const std::vector<Level>& x, const std::vector<Level>& y) {
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); ++i) {
        if (x[i].price != y[i].price || x[i].qty != y[i].qty) return false;
    }
    return true;
}

int verify(const std::vector<Op>& ops) {
    BookMap a;
    BookHeap b;
    BookArray c;

    bool ok_b = true, ok_c = true;
    size_t first_bad = 0;

    std::printf("Running correctness test with %zu operations...\n\n", ops.size());

    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        switch (op.type) {
            case OpType::Add:
                a.add_order(op.id, op.side, op.price, op.qty);
                b.add_order(op.id, op.side, op.price, op.qty);
                c.add_order(op.id, op.side, op.price, op.qty);
                break;
            case OpType::Cancel:
                a.cancel_order(op.id);
                b.cancel_order(op.id);
                c.cancel_order(op.id);
                break;
            case OpType::Bbo:
                break;
        }

        BBO ba = a.get_bbo(), bb = b.get_bbo(), bc = c.get_bbo();
        std::vector<Level> abids = a.get_bids(kDepth), bbids = b.get_bids(kDepth), cbids = c.get_bids(kDepth);
        std::vector<Level> aasks = a.get_asks(kDepth), basks = b.get_asks(kDepth), casks = c.get_asks(kDepth);

        bool b_match = bbo_equal(ba, bb) && levels_equal(abids, bbids) && levels_equal(aasks, basks);
        bool c_match = bbo_equal(ba, bc) && levels_equal(abids, cbids) && levels_equal(aasks, casks);

        if (!b_match && ok_b) { ok_b = false; first_bad = i; }
        if (!c_match && ok_c) { ok_c = false; first_bad = i; }
        if (!ok_b || !ok_c) break;
    }

    std::printf("Implementation A: PASS (reference)\n");
    std::printf("Implementation B: %s\n", ok_b ? "PASS" : "FAIL");
    std::printf("Implementation C: %s\n", ok_c ? "PASS" : "FAIL");

    if (ok_b && ok_c) {
        std::printf("\nAll implementations produce identical results.\n");
        return 0;
    }
    std::printf("\nMismatch first seen at operation %zu.\n", first_bad);
    return 1;
}

// ---- small edge-case tests ----------------------------------------------------

int g_failures = 0;
void check(bool cond, const char* what) {
    std::printf("  [%s] %s\n", cond ? "ok" : "FAIL", what);
    if (!cond) ++g_failures;
}

template <typename Book>
void test_one(const char* label) {
    std::printf("%s\n", label);

    { // empty book
        Book book;
        BBO b = book.get_bbo();
        check(!b.has_bid && !b.has_ask, "empty book has no bid/ask");
        check(book.get_bids().empty() && book.get_asks().empty(), "empty book has no levels");
        check(book.cancel_order(123) == false, "cancel on empty book returns false");
    }
    { // aggregation at a price level
        Book book;
        book.add_order(1, Side::Buy, 99.98, 100);
        book.add_order(2, Side::Buy, 99.98, 50);
        BBO b = book.get_bbo();
        check(b.has_bid && b.bid_price == 99.98 && b.bid_qty == 150, "quantities aggregate at a price");
    }
    { // insufficient depth
        Book book;
        book.add_order(1, Side::Sell, 100.01, 10);
        book.add_order(2, Side::Sell, 100.02, 20);
        book.add_order(3, Side::Sell, 100.03, 30);
        check(book.get_asks(10).size() == 3, "depth request returns available levels");
    }
    { // unknown / repeated cancel
        Book book;
        book.add_order(1, Side::Buy, 55.00, 10);
        check(book.cancel_order(1) == true, "first cancel succeeds");
        check(book.cancel_order(1) == false, "second cancel returns false");
        check(book.cancel_order(999) == false, "cancel of unknown id returns false");
    }
    { // best level removal moves the BBO
        Book book;
        book.add_order(1, Side::Buy, 60.00, 10);
        book.add_order(2, Side::Buy, 61.00, 10);
        BBO top = book.get_bbo();
        check(top.bid_price == 61.00, "best bid is the highest price");
        book.cancel_order(2);
        BBO next = book.get_bbo();
        check(next.bid_price == 60.00, "best bid drops after the top empties");
    }
    { // invalid inputs throw
        Book book;
        bool threw = false;
        try { book.add_order(1, Side::Buy, 100.01, 10); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "buy outside the bid band throws");
        threw = false;
        try { book.add_order(2, Side::Buy, 55.005, 10); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "non-tick price throws");
        threw = false;
        try { book.add_order(3, Side::Sell, 120.00, 0); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "non-positive quantity throws");
        threw = false;
        book.add_order(4, Side::Buy, 55.00, 10);
        try { book.add_order(4, Side::Buy, 55.00, 10); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "duplicate order id throws");
    }
}

int run_tests() {
    test_one<BookMap>("Implementation A");
    test_one<BookHeap>("Implementation B");
    test_one<BookArray>("Implementation C");
    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    std::string impl;
    bool do_verify = false, do_test = false;
    size_t n_ops = 1000000;
    int runs = 5;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--impl" && i + 1 < argc) {
            impl = argv[++i];
        } else if (arg == "--verify") {
            do_verify = true;
        } else if (arg == "--test") {
            do_test = true;
        } else if (arg == "--ops" && i + 1 < argc) {
            n_ops = std::stoull(argv[++i]);
        } else if (arg == "--runs" && i + 1 < argc) {
            runs = std::stoi(argv[++i]);
        } else {
            std::fprintf(stderr, "unknown or incomplete argument: %s\n", arg.c_str());
            return 2;
        }
    }

    if (do_test) {
        return run_tests();
    }

    std::vector<Op> ops = build_workload(n_ops);

    if (do_verify) {
        return verify(ops);
    }

    if (impl == "A") return benchmark<BookMap>(ops, runs);
    if (impl == "B") return benchmark<BookHeap>(ops, runs);
    if (impl == "C") return benchmark<BookArray>(ops, runs);

    std::fprintf(stderr,
        "usage: %s --impl A|B|C | --verify | --test [--ops N] [--runs N]\n", argv[0]);
    return 2;
}
