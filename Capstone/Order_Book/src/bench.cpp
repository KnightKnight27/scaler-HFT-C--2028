// One benchmark program for all three order books.
//
//   ./bench --impl A|B|C     run the benchmark for one implementation
//   ./bench --verify         correctness test (A, B, C must agree)
//
// Options: --runs N (default 5), --ops N (default 1000000), --total-only, --gen-only
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "book_a_map.hpp"
#include "book_b_vector.hpp"
#include "book_c_array.hpp"
#include "verify.hpp"
#include "workload.hpp"

using Clock = std::chrono::steady_clock;

struct RunResult {
    double total_ms = 0;
    double ns_add = 0, ns_cancel = 0, ns_bbo = 0, ns_depth = 0;
    BBO final_bbo;
    uint64_t checksum = 0;
    size_t live = 0;
};

// ---- checksum, so the compiler can't throw the work away -----------------

inline void mix(uint64_t& h, uint64_t v) { h = (h ^ v) * 1099511628211ull; }

inline void mix_bbo(uint64_t& h, const BBO& b) {
    mix(h, b.has_bid);
    mix(h, b.bid_price);
    mix(h, b.bid_qty);
    mix(h, b.has_ask);
    mix(h, b.ask_price);
    mix(h, b.ask_qty);
}

inline void mix_levels(uint64_t& h, const std::vector<Level>& v) {
    mix(h, v.size());
    for (const Level& l : v) {
        mix(h, l.price);
        mix(h, l.qty);
    }
}

// ---- replaying the workload ----------------------------------------------

// Replays all ops on the book. With Timed = false nothing is measured inside
// the loop (used for the total time and for perf). With Timed = true every
// call is wrapped in steady_clock::now() so we get ns per operation type.
struct Timers {
    long long add_ns = 0, cancel_ns = 0, bbo_ns = 0, depth_ns = 0;
    long add_n = 0, cancel_n = 0, bbo_n = 0, depth_n = 0;
};

template <bool Timed, class Book>
uint64_t replay(Book& book, const std::vector<Op>& ops, Timers& t) {
    uint64_t sum = 14695981039346656037ull;
    long count = 0;

    for (const Op& op : ops) {
        Clock::time_point t0;
        if (Timed) t0 = Clock::now();

        switch (op.type) {
            case OpType::Add:
                mix(sum, book.add_order(op.id, op.side, op.tick, op.qty));
                break;
            case OpType::Cancel:
                mix(sum, book.cancel_order(op.id));
                break;
            case OpType::Bbo:
                mix_bbo(sum, book.get_bbo());
                break;
        }

        if (Timed) {
            long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
            if (op.type == OpType::Add) { t.add_ns += ns; t.add_n++; }
            else if (op.type == OpType::Cancel) { t.cancel_ns += ns; t.cancel_n++; }
            else { t.bbo_ns += ns; t.bbo_n++; }
        }

        if (++count % kDepthEvery == 0) {
            Clock::time_point d0;
            if (Timed) d0 = Clock::now();

            std::vector<Level> bids = book.get_bids(kDepth);
            std::vector<Level> asks = book.get_asks(kDepth);
            mix_levels(sum, bids);
            mix_levels(sum, asks);

            if (Timed) {
                t.depth_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - d0).count();
                t.depth_n++;
            }
        }
    }
    return sum;
}

// Cost of one now() ... now() pair, so it can be taken out of the per-op numbers.
double timer_overhead_ns() {
    const int n = 1000000;
    long long total = 0;
    for (int i = 0; i < n; i++) {
        auto a = Clock::now();
        auto b = Clock::now();
        total += std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
    }
    return (double)total / n;
}

template <class Book>
RunResult run_once(const std::vector<Op>& ops, bool breakdown, double overhead_ns) {
    RunResult r;

    {   // pass 1: total time, no clock calls inside the loop
        auto book = std::make_unique<Book>();
        Timers unused;
        auto start = Clock::now();
        r.checksum = replay<false>(*book, ops, unused);
        auto end = Clock::now();
        r.total_ms = std::chrono::duration<double, std::milli>(end - start).count();
        r.final_bbo = book->get_bbo();
        r.live = book->live_orders();
    }

    if (breakdown) {  // pass 2: a fresh book, every call timed
        auto book = std::make_unique<Book>();
        Timers t;
        replay<true>(*book, ops, t);
        auto per_op = [&](long long ns, long n) {
            return std::max(0.0, (double)ns / n - overhead_ns);
        };
        r.ns_add = per_op(t.add_ns, t.add_n);
        r.ns_cancel = per_op(t.cancel_ns, t.cancel_n);
        r.ns_bbo = per_op(t.bbo_ns, t.bbo_n);
        r.ns_depth = per_op(t.depth_ns, t.depth_n);
    }
    return r;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

template <class Book>
void run_benchmark(const char* name, const char* what, const std::vector<Op>& ops, int runs,
                   bool breakdown) {
    long adds = 0, cancels = 0, bbos = 0;
    for (const Op& op : ops) {
        if (op.type == OpType::Add) adds++;
        else if (op.type == OpType::Cancel) cancels++;
        else bbos++;
    }

    double overhead = breakdown ? timer_overhead_ns() : 0;

    std::vector<double> total, add, cancel, bbo, depth;
    RunResult last;
    for (int i = 0; i < runs; i++) {
        last = run_once<Book>(ops, breakdown, overhead);
        total.push_back(last.total_ms);
        add.push_back(last.ns_add);
        cancel.push_back(last.ns_cancel);
        bbo.push_back(last.ns_bbo);
        depth.push_back(last.ns_depth);
    }

    printf("Implementation %s: %s\n", name, what);
    printf("Operations: %zu (%ld add, %ld cancel, %ld bbo) + %zu depth queries, seed %d\n",
           ops.size(), adds, cancels, bbos, ops.size() / kDepthEvery, kSeed);
    printf("Runs: %d, median reported\n\n", runs);

    printf("Total time        : %.2f ms   (%.1f ns per op on average)\n", median(total),
           median(total) * 1e6 / ops.size());
    if (breakdown) {
        printf("ns / add_order    : %.1f\n", median(add));
        printf("ns / cancel_order : %.1f\n", median(cancel));
        printf("ns / get_bbo      : %.1f\n", median(bbo));
        printf("ns / depth query  : %.1f   (get_bids(10) + get_asks(10))\n", median(depth));
        printf("(timer overhead of %.1f ns per call already subtracted)\n", overhead);
    }

    const BBO& b = last.final_bbo;
    printf("\nFinal BBO:\n");
    if (b.has_bid) printf("  Bid: %.2f x %ld\n", ticks_to_price(b.bid_price), b.bid_qty);
    else printf("  Bid: none\n");
    if (b.has_ask) printf("  Ask: %.2f x %ld\n", ticks_to_price(b.ask_price), b.ask_qty);
    else printf("  Ask: none\n");
    printf("Live orders: %zu\n", last.live);
    printf("Checksum: %llu\n", (unsigned long long)last.checksum);
}

void usage(const char* prog) {
    printf("usage: %s --impl A|B|C [--runs N] [--ops N] [--total-only]\n", prog);
    printf("       %s --verify [--ops N]\n", prog);
    printf("  A = std::map + unordered_map\n");
    printf("  B = sorted vector + std::map\n");
    printf("  C = price indexed array + bitmap + flat hash table\n");
    printf("  --total-only skips the per-operation timing pass (use this with perf)\n");
    printf("  --gen-only only builds the workload (baseline for perf, see REPORT.md)\n");
}

int main(int argc, char** argv) {
    std::string impl;
    bool verify = false, breakdown = true, gen_only = false;
    int runs = 5;
    int num_ops = kDefaultOps;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--impl") && i + 1 < argc) impl = argv[++i];
        else if (!strcmp(argv[i], "--runs") && i + 1 < argc) runs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ops") && i + 1 < argc) num_ops = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--verify")) verify = true;
        else if (!strcmp(argv[i], "--total-only")) breakdown = false;
        else if (!strcmp(argv[i], "--gen-only")) gen_only = true;
        else { usage(argv[0]); return 1; }
    }
    if (runs < 1 || num_ops < 1) { usage(argv[0]); return 1; }

    if (verify) return run_verify(num_ops) ? 0 : 1;

    if (gen_only) {
        std::vector<Op> ops = make_workload(num_ops);
        printf("built %zu ops\n", ops.size());
        return 0;
    }

    if (impl != "A" && impl != "B" && impl != "C") {
        usage(argv[0]);
        return 1;
    }

    std::vector<Op> ops = make_workload(num_ops);

    if (impl == "A")
        run_benchmark<MapBook>("A", "std::map + std::unordered_map", ops, runs, breakdown);
    else if (impl == "B")
        run_benchmark<VectorBook>("B", "sorted std::vector + std::map", ops, runs, breakdown);
    else
        run_benchmark<ArrayBook>("C", "price indexed array + bitmap + flat hash table", ops, runs, breakdown);
    return 0;
}
