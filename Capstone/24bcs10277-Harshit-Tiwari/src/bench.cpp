#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "book_array.hpp"
#include "book_map.hpp"
#include "book_vector.hpp"
#include "verify.hpp"
#include "workload.hpp"

using Clock = std::chrono::steady_clock;

// Keeps the compiler from optimising the timed calls away.
volatile long g_sink;

static long long ns_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
}

static long long percentile(std::vector<uint32_t>& v, double p) {
    if (v.empty()) return 0;
    size_t k = (size_t)(p * (v.size() - 1));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

struct RunResult {
    double total_ms;
    double add_ns, cancel_ns, bbo_ns, depth_ns;
    double add_p50, cancel_p50, bbo_p50;
    double add_p99, cancel_p99, bbo_p99;
    uint64_t checksum;
    BBO final_bbo;
};

static void mix(uint64_t& sum, long long x) { sum = sum * 1000003ULL + (uint64_t)x; }

template <class Book>
static double run_total(const std::vector<Op>& ops, uint64_t& checksum, BBO& final_bbo) {
    Book book;
    uint64_t sum = 0;
    auto start = Clock::now();
    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        if (op.type == OpType::Add) {
            mix(sum, book.add_order(op.id, op.side, op.price, op.qty));
        } else if (op.type == OpType::Cancel) {
            mix(sum, book.cancel_order(op.id));
        } else {
            BBO b = book.get_bbo();
            mix(sum, b.has_bid ? b.bid_qty + to_tick(b.bid_price) : 0);
            mix(sum, b.has_ask ? b.ask_qty + to_tick(b.ask_price) : 0);
        }
        if ((i + 1) % 100 == 0) {
            for (const Level& l : book.get_bids(10)) mix(sum, l.qty + to_tick(l.price));
            for (const Level& l : book.get_asks(10)) mix(sum, l.qty + to_tick(l.price));
        }
    }
    double ms = ns_between(start, Clock::now()) / 1e6;
    checksum = sum;
    final_bbo = book.get_bbo();
    return ms;
}

template <class Book>
static void run_per_op(const std::vector<Op>& ops, RunResult& r) {
    Book book;
    std::vector<uint32_t> add, cancel, bbo;
    add.reserve(ops.size());
    cancel.reserve(ops.size());
    bbo.reserve(ops.size());
    long long depth_total = 0, depth_calls = 0;
    long sink = 0;

    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        auto t0 = Clock::now();
        if (op.type == OpType::Add) {
            sink += book.add_order(op.id, op.side, op.price, op.qty);
            add.push_back((uint32_t)ns_between(t0, Clock::now()));
        } else if (op.type == OpType::Cancel) {
            sink += book.cancel_order(op.id);
            cancel.push_back((uint32_t)ns_between(t0, Clock::now()));
        } else {
            sink += book.get_bbo().bid_qty;
            bbo.push_back((uint32_t)ns_between(t0, Clock::now()));
        }
        if ((i + 1) % 100 == 0) {
            auto d0 = Clock::now();
            sink += book.get_bids(10).size() + book.get_asks(10).size();
            depth_total += ns_between(d0, Clock::now());
            depth_calls++;
        }
    }

    auto mean = [](const std::vector<uint32_t>& v) {
        double s = 0;
        for (uint32_t x : v) s += x;
        return v.empty() ? 0.0 : s / v.size();
    };
    r.add_ns = mean(add);
    r.cancel_ns = mean(cancel);
    r.bbo_ns = mean(bbo);
    r.depth_ns = depth_calls ? (double)depth_total / depth_calls : 0.0;
    r.add_p50 = percentile(add, 0.50);
    r.cancel_p50 = percentile(cancel, 0.50);
    r.bbo_p50 = percentile(bbo, 0.50);
    r.add_p99 = percentile(add, 0.99);
    r.cancel_p99 = percentile(cancel, 0.99);
    r.bbo_p99 = percentile(bbo, 0.99);
    g_sink = sink;
}

static double timer_overhead_ns() {
    long long total = 0;
    const int n = 200000;
    for (int i = 0; i < n; i++) {
        auto a = Clock::now();
        total += ns_between(a, Clock::now());
    }
    return (double)total / n;
}

static double median(std::vector<RunResult>& runs, double RunResult::*field) {
    std::vector<double> v;
    for (const RunResult& r : runs) v.push_back(r.*field);
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

template <class Book>
static int run_bench(const char* label, int num_runs) {
    WorkloadConfig cfg;
    std::vector<Op> ops = make_workload(cfg);

    long adds = 0, cancels = 0, bbos = 0;
    for (const Op& op : ops) {
        if (op.type == OpType::Add) adds++;
        else if (op.type == OpType::Cancel) cancels++;
        else bbos++;
    }

    std::vector<RunResult> runs(num_runs);
    for (RunResult& r : runs) {
        r.total_ms = run_total<Book>(ops, r.checksum, r.final_bbo);
        run_per_op<Book>(ops, r);
    }

    std::printf("Implementation %s\n", label);
    std::printf("Workload: %zu ops (seed %u) = %ld add, %ld cancel, %ld bbo, plus get_bids(10)+get_asks(10) every 100 ops\n",
                ops.size(), cfg.seed, adds, cancels, bbos);
    std::printf("Runs: %d (median reported)   timer overhead: ~%.0f ns per measured call\n\n", num_runs,
                timer_overhead_ns());

    std::printf("Total time (ms):    %8.2f\n\n", median(runs, &RunResult::total_ms));
    std::printf("                        avg      p50      p99\n");
    std::printf("ns / add_order:    %8.1f %8.0f %8.0f\n", median(runs, &RunResult::add_ns),
                median(runs, &RunResult::add_p50), median(runs, &RunResult::add_p99));
    std::printf("ns / cancel_order: %8.1f %8.0f %8.0f\n", median(runs, &RunResult::cancel_ns),
                median(runs, &RunResult::cancel_p50), median(runs, &RunResult::cancel_p99));
    std::printf("ns / get_bbo:      %8.1f %8.0f %8.0f\n", median(runs, &RunResult::bbo_ns),
                median(runs, &RunResult::bbo_p50), median(runs, &RunResult::bbo_p99));
    std::printf("ns / bids+asks(10):%8.1f\n\n", median(runs, &RunResult::depth_ns));

    const RunResult& last = runs.back();
    std::printf("Final BBO:\n");
    if (last.final_bbo.has_bid) std::printf("  Bid: %.2f x %ld\n", last.final_bbo.bid_price, last.final_bbo.bid_qty);
    else std::printf("  Bid: none\n");
    if (last.final_bbo.has_ask) std::printf("  Ask: %.2f x %ld\n", last.final_bbo.ask_price, last.final_bbo.ask_qty);
    else std::printf("  Ask: none\n");
    std::printf("Checksum: %llu\n", (unsigned long long)last.checksum);
    return 0;
}

static int usage() {
    std::printf("usage: bench --verify\n"
                "       bench --impl A|B|C [--runs N]\n");
    return 2;
}

int main(int argc, char** argv) {
    std::string impl;
    int runs = 5;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--verify")) return run_verify();
        if (!std::strcmp(argv[i], "--impl") && i + 1 < argc) impl = argv[++i];
        else if (!std::strcmp(argv[i], "--runs") && i + 1 < argc) runs = std::max(1, std::atoi(argv[++i]));
        else return usage();
    }

    if (impl == "A") return run_bench<MapBook>("A: std::map levels + std::unordered_map orders", runs);
    if (impl == "B") return run_bench<VectorBook>("B: sorted std::vector levels + std::map orders", runs);
    if (impl == "C") return run_bench<ArrayBook>("C: flat tick array + open-addressing order table", runs);
    return usage();
}
