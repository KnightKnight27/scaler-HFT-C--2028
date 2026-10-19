// Benchmark + correctness driver.
//
//   ./bench --verify                 run A, B, C in lock-step and compare
//   ./bench --impl A|B|C [options]   time one implementation
//   ./bench --impl none [options]    empty book, measures the loop itself
//
//   --ops N      number of ops (default 1,000,000)
//   --seed S     mt19937 seed (default 42)
//   --reps R     repetitions, median is printed (default 5)
//   --no-timers  don't time individual ops (for perf)
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "book_array.hpp"
#include "book_heap.hpp"
#include "book_map.hpp"
#include "common.hpp"
#include "runner.hpp"
#include "workload.hpp"

namespace {

struct Options {
    std::string impl;
    bool verify = false;
    bool timers = true;
    size_t ops = 1'000'000;
    uint32_t seed = 42;
    int reps = 5;
};

[[noreturn]] void usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s --verify | --impl A|B|C|none [--ops N] [--seed S] [--reps R] [--no-timers]\n",
                 argv0);
    std::exit(2);
}

Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", flag);
                usage(argv[0]);
            }
            return argv[++i];
        };
        if (a == "--verify") o.verify = true;
        else if (a == "--impl") o.impl = need("--impl");
        else if (a == "--ops") o.ops = std::strtoull(need("--ops"), nullptr, 10);
        else if (a == "--seed") o.seed = static_cast<uint32_t>(std::strtoul(need("--seed"), nullptr, 10));
        else if (a == "--reps") o.reps = std::atoi(need("--reps"));
        else if (a == "--no-timers") o.timers = false;
        else usage(argv[0]);
    }
    if (!o.verify && o.impl.empty()) usage(argv[0]);
    if (!o.impl.empty() && o.impl != "A" && o.impl != "B" && o.impl != "C" && o.impl != "none") usage(argv[0]);
    if (o.reps < 1) o.reps = 1;
    return o;
}

// ---------------------------------------------------------------------------
// benchmark
using ob::RunResult;
using ob::fold_bbo;
using ob::fold_levels;
using ob::mix;

// Empty book, so --impl none measures only the loop, timers and checksum.
struct NullBook {
    static constexpr const char* name() { return "none: empty harness baseline"; }
    void add_order(uint64_t, Side, double, long) {}
    bool cancel_order(uint64_t) { return false; }
    BBO get_bbo() const { return ob::empty_bbo(); }
    std::vector<Level> get_bids(int) const { return {}; }
    std::vector<Level> get_asks(int) const { return {}; }
};

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

void print_bbo(const BBO& b) {
    if (b.has_bid) std::printf("  Bid: %.2f x %ld\n", b.bid_price, b.bid_qty);
    else std::printf("  Bid: (none)\n");
    if (b.has_ask) std::printf("  Ask: %.2f x %ld\n", b.ask_price, b.ask_qty);
    else std::printf("  Ask: (none)\n");
}

template <class Book>
int run_benchmark(const Options& o) {
    ob::WorkloadStats st;
    const std::vector<ob::Op> ops = ob::make_workload(o.ops, o.seed, &st);

    std::printf("Implementation %s\n", Book::name());
    std::printf("Workload: %zu ops (seed %u): %zu adds, %zu cancels, %zu bbo queries, "
                "+ %zu depth snapshots (get_bids(10)+get_asks(10))\n",
                o.ops, o.seed, st.adds, st.cancels, st.bbos, st.depths);
    if (o.timers) std::printf("Timer overhead (steady_clock pair): %.1f ns, included in per-op numbers\n",
                              ob::timer_overhead_ns());
    std::printf("Runs: %d, reporting the median\n\n", o.reps);

    std::vector<RunResult> runs;
    for (int i = 0; i < o.reps; ++i) {
        RunResult r = o.timers ? ob::run_once<Book, true>(ops) : ob::run_once<Book, false>(ops);
        if (o.timers) {
            std::printf("run %d: total %8.2f ms | add %7.1f ns | cancel %7.1f ns | bbo %6.1f ns | depth %7.1f ns\n",
                        i + 1, r.total_ms, r.add_ns, r.cancel_ns, r.bbo_ns, r.depth_ns);
        } else {
            std::printf("run %d: total %8.2f ms\n", i + 1, r.total_ms);
        }
        runs.push_back(r);
    }

    auto med = [&](double RunResult::*f) {
        std::vector<double> v;
        for (const RunResult& r : runs) v.push_back(r.*f);
        return median(v);
    };

    std::printf("\nMedian of %d runs\n", o.reps);
    std::printf("  Total time      : %.2f ms\n", med(&RunResult::total_ms));
    if (o.timers) {
        std::printf("  ns / add_order  : %.1f\n", med(&RunResult::add_ns));
        std::printf("  ns / cancel     : %.1f\n", med(&RunResult::cancel_ns));
        std::printf("  ns / get_bbo    : %.1f\n", med(&RunResult::bbo_ns));
        std::printf("  ns / depth(10)  : %.1f  (get_bids(10) + get_asks(10))\n", med(&RunResult::depth_ns));
    }

    std::printf("\nFinal BBO:\n");
    print_bbo(runs.front().final_bbo);
    std::printf("\nChecksum: %llu\n", static_cast<unsigned long long>(runs.front().checksum));

    for (const RunResult& r : runs) {
        if (r.checksum != runs.front().checksum) {
            std::printf("ERROR: checksum differs between runs\n");
            return 1;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// correctness
bool same_bbo(const BBO& a, const BBO& b) {
    if (a.has_bid != b.has_bid || a.has_ask != b.has_ask) return false;
    if (a.has_bid && (ob::to_tick(a.bid_price) != ob::to_tick(b.bid_price) || a.bid_qty != b.bid_qty)) return false;
    if (a.has_ask && (ob::to_tick(a.ask_price) != ob::to_tick(b.ask_price) || a.ask_qty != b.ask_qty)) return false;
    return true;
}

bool same_levels(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (ob::to_tick(a[i].price) != ob::to_tick(b[i].price) || a[i].qty != b[i].qty) return false;
    }
    return true;
}

// Edge cases from the spec.
template <class Book>
bool edge_cases(const char* label, std::vector<std::string>& failures) {
    bool ok = true;
    auto check = [&](bool cond, const char* what) {
        if (!cond) {
            ok = false;
            failures.push_back(std::string(label) + ": " + what);
        }
    };

    Book book;
    const BBO empty = book.get_bbo();
    check(!empty.has_bid && !empty.has_ask, "empty book must report no bid and no ask");
    check(book.get_bids(10).empty() && book.get_asks(10).empty(), "empty book depth must be empty");
    check(!book.cancel_order(12345), "cancelling an unknown id must return false");

    book.add_order(1, Side::Buy, 99.99, 10);
    book.add_order(2, Side::Buy, 99.99, 5);   // same price, qty aggregates
    book.add_order(3, Side::Buy, 98.50, 7);
    book.add_order(4, Side::Buy, 50.00, 1);
    book.add_order(5, Side::Sell, 100.01, 3);
    book.add_order(6, Side::Sell, 150.00, 9);

    BBO b = book.get_bbo();
    check(b.has_bid && ob::to_tick(b.bid_price) == 9999 && b.bid_qty == 15, "best bid must be 99.99 x 15");
    check(b.has_ask && ob::to_tick(b.ask_price) == 10001 && b.ask_qty == 3, "best ask must be 100.01 x 3");

    std::vector<Level> bids = book.get_bids(10);
    check(bids.size() == 3, "get_bids(10) with 3 levels must return 3 levels");
    check(bids.size() == 3 && ob::to_tick(bids[0].price) == 9999 && ob::to_tick(bids[1].price) == 9850 &&
              ob::to_tick(bids[2].price) == 5000,
          "bids must be sorted high to low");
    std::vector<Level> asks = book.get_asks(1);
    check(asks.size() == 1 && ob::to_tick(asks[0].price) == 10001, "get_asks(1) must return only the best ask");

    check(book.cancel_order(1), "cancelling a live order must return true");
    check(!book.cancel_order(1), "cancelling the same order twice must return false");
    b = book.get_bbo();
    check(b.has_bid && b.bid_qty == 5, "best bid qty must drop after a partial-level cancel");

    check(book.cancel_order(2) && book.cancel_order(3) && book.cancel_order(4), "cancel remaining bids");
    b = book.get_bbo();
    check(!b.has_bid && b.has_ask, "book with no bids must report has_bid == false");
    check(book.cancel_order(5), "cancel best ask");
    b = book.get_bbo();
    check(b.has_ask && ob::to_tick(b.ask_price) == 15000 && b.ask_qty == 9, "best ask must move to 150.00 x 9");
    check(book.cancel_order(6), "cancel last ask");
    b = book.get_bbo();
    check(!b.has_bid && !b.has_ask, "fully cancelled book must be empty again");

    return ok;
}

int run_verify(const Options& o) {
    std::printf("Running correctness test...\n\n");

    std::vector<std::string> failures;
    bool okA = edge_cases<MapBook>("A", failures);
    bool okB = edge_cases<HeapBook>("B", failures);
    bool okC = edge_cases<ArrayBook>("C", failures);

    // Replay in lock-step. A is the reference, B and C must match after every op.
    const std::vector<ob::Op> ops = ob::make_workload(o.ops, o.seed);
    MapBook a;
    HeapBook b;
    ArrayBook c;
    size_t bbo_checks = 0, depth_checks = 0, cancel_checks = 0;

    auto mismatch = [&](char impl, size_t i, const char* what) {
        if (impl == 'B') okB = false; else okC = false;
        failures.push_back(std::string(1, impl) + " disagrees with A at op " + std::to_string(i) + ": " + what);
    };

    for (size_t i = 0; i < ops.size(); ++i) {
        const ob::Op& op = ops[i];
        switch (op.type) {
            case ob::OpType::Add: {
                const double price = ob::to_price(op.tick);
                a.add_order(op.id, op.side, price, op.qty);
                b.add_order(op.id, op.side, price, op.qty);
                c.add_order(op.id, op.side, price, op.qty);
                break;
            }
            case ob::OpType::Cancel: {
                const bool ra = a.cancel_order(op.id);
                const bool rb = b.cancel_order(op.id);
                const bool rc = c.cancel_order(op.id);
                if (rb != ra) mismatch('B', i, "cancel_order result");
                if (rc != ra) mismatch('C', i, "cancel_order result");
                if (!ra) { okA = false; failures.push_back("A: cancel of a live order returned false"); }
                ++cancel_checks;
                break;
            }
            case ob::OpType::Bbo:
            case ob::OpType::Depth:
                break;
        }
        // compare the BBO after every op, not just on bbo ops
        const BBO ba = a.get_bbo();
        if (!same_bbo(ba, b.get_bbo())) mismatch('B', i, "BBO");
        if (!same_bbo(ba, c.get_bbo())) mismatch('C', i, "BBO");
        ++bbo_checks;

        if (op.type == ob::OpType::Depth) {
            // top 10 like the benchmark, and the whole book
            for (int depth : {10, 1 << 20}) {
                const std::vector<Level> bids_a = a.get_bids(depth), asks_a = a.get_asks(depth);
                if (!same_levels(bids_a, b.get_bids(depth))) mismatch('B', i, "bid levels");
                if (!same_levels(asks_a, b.get_asks(depth))) mismatch('B', i, "ask levels");
                if (!same_levels(bids_a, c.get_bids(depth))) mismatch('C', i, "bid levels");
                if (!same_levels(asks_a, c.get_asks(depth))) mismatch('C', i, "ask levels");
            }
            ++depth_checks;
        }
        if (failures.size() > 20) break;
    }

    std::printf("Workload: %zu ops, seed %u\n", o.ops, o.seed);
    std::printf("Checked : %zu BBO snapshots, %zu cancel results, %zu depth snapshots "
                "(top-10 and full book), plus edge cases\n\n",
                bbo_checks, cancel_checks, depth_checks);
    std::printf("Implementation A: %s\n", okA ? "PASS" : "FAIL");
    std::printf("Implementation B: %s\n", okB ? "PASS" : "FAIL");
    std::printf("Implementation C: %s\n\n", okC ? "PASS" : "FAIL");

    if (okA && okB && okC) {
        std::printf("All implementations produce identical results.\n");
        return 0;
    }
    std::printf("Mismatches:\n");
    for (const std::string& f : failures) std::printf("  - %s\n", f.c_str());
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    const Options o = parse_args(argc, argv);
    if (o.verify) return run_verify(o);
    if (o.impl == "A") return run_benchmark<MapBook>(o);
    if (o.impl == "B") return run_benchmark<HeapBook>(o);
    if (o.impl == "C") return run_benchmark<ArrayBook>(o);
    return run_benchmark<NullBook>(o);
}
