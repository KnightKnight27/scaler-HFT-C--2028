// Benchmark program: one executable, implementation chosen on the command line.
//   Build: g++ -std=c++17 -O2 -o bench benchmark/bench.cpp
//   Run  : ./bench --impl A|B|C   [--runs N] [--ops N] [--no-op-timing]
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "../src/order_book_a.hpp"
#include "../src/order_book_b.hpp"
#include "../src/order_book_c.hpp"
#include "../src/workload.hpp"

// ============================================================ 7. Benchmark
using Clock = std::chrono::steady_clock;

struct Result {
    double total_ms = 0, add_ns = 0, cancel_ns = 0, bbo_ns = 0;
    unsigned long long checksum = 0;
    BBO final_bbo{};
};

static void mix(unsigned long long& cs, unsigned long long v) { cs = cs * 1099511628211ULL + v; }
static void mix_bbo(unsigned long long& cs, const BBO& b) {
    mix(cs, b.has_bid); mix(cs, to_tick(b.bid_price)); mix(cs, b.bid_qty);
    mix(cs, b.has_ask); mix(cs, to_tick(b.ask_price)); mix(cs, b.ask_qty);
}
static void mix_levels(unsigned long long& cs, const std::vector<Level>& v) {
    for (auto& l : v) { mix(cs, to_tick(l.price)); mix(cs, l.qty); }
    mix(cs, v.size());
}

// timed == false : only the whole loop is timed (-> total execution time)
// timed == true  : every call is timed separately (-> ns per add / cancel / bbo)
template <class Book>
Result run_once(const std::vector<Op>& w, bool timed, double clock_ns) {
    auto book = std::make_unique<Book>();
    Result r;
    double t_add = 0, t_can = 0, t_bbo = 0;
    long n_add = 0, n_can = 0, n_bbo = 0;
    auto ns = [](Clock::time_point a, Clock::time_point b) {
        return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
    };

    auto start = Clock::now();
    for (size_t i = 0; i < w.size(); ++i) {
        const Op& op = w[i];
        if (op.type == OP_ADD) {
            const auto a = timed ? Clock::now() : Clock::time_point();
            book->add_order(op.id, op.side, op.price, op.qty);
            if (timed) { t_add += ns(a, Clock::now()); ++n_add; }
        } else if (op.type == OP_CANCEL) {
            const auto a = timed ? Clock::now() : Clock::time_point();
            bool ok = book->cancel_order(op.id);
            if (timed) { t_can += ns(a, Clock::now()); ++n_can; }
            mix(r.checksum, ok);
        } else {
            const auto a = timed ? Clock::now() : Clock::time_point();
            BBO b = book->get_bbo();
            if (timed) { t_bbo += ns(a, Clock::now()); ++n_bbo; }
            mix_bbo(r.checksum, b);
        }
        if ((i + 1) % 100 == 0) {                       // every 100 ops: depth queries
            mix_levels(r.checksum, book->get_bids(10));
            mix_levels(r.checksum, book->get_asks(10));
        }
    }
    r.total_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    r.final_bbo = book->get_bbo();
    mix_bbo(r.checksum, r.final_bbo);
    if (timed) {                                        // subtract the cost of the two clock reads
        r.add_ns    = std::max(0.0, t_add / n_add - clock_ns);
        r.cancel_ns = std::max(0.0, t_can / n_can - clock_ns);
        r.bbo_ns    = std::max(0.0, t_bbo / n_bbo - clock_ns);
    }
    return r;
}

double clock_overhead_ns() {                            // cost of "now(); now();" back to back
    const int N = 1000000;
    double total = 0;
    for (int i = 0; i < N; ++i) {
        auto a = Clock::now();
        auto b = Clock::now();
        total += (double)std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
    }
    return total / N;
}

double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

template <class Book>
void benchmark(const char* name, const std::vector<Op>& w, int runs, bool op_timing, double clock_ns) {
    std::vector<double> total, add, can, bbo;
    Result last;
    for (int i = 0; i < runs; ++i) {
        last = run_once<Book>(w, false, clock_ns);                    // pass 1: total time
        total.push_back(last.total_ms);
        if (op_timing) {                                              // pass 2: per-operation time
            Result t = run_once<Book>(w, true, clock_ns);
            add.push_back(t.add_ns); can.push_back(t.cancel_ns); bbo.push_back(t.bbo_ns);
        }
    }
    std::printf("=== Implementation %s (median of %d runs, %zu ops) ===\n", name, runs, w.size());
    std::printf("Total time (ms) : %.2f\n", median(total));
    if (op_timing) {
        std::printf("ns / add_order  : %.1f\n", median(add));
        std::printf("ns / cancel     : %.1f\n", median(can));
        std::printf("ns / get_bbo    : %.1f\n", median(bbo));
    }
    std::printf("Final BBO:\n  Bid: %.2f x %ld\n  Ask: %.2f x %ld\n", last.final_bbo.bid_price, last.final_bbo.bid_qty,
                last.final_bbo.ask_price, last.final_bbo.ask_qty);
    std::printf("Checksum: %llu\n", last.checksum);
}

// ============================================================ 9. main
int main(int argc, char** argv) {
    std::string impl;
    bool op_timing = true;
    int runs = 5;
    size_t ops = 1000000;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--impl" && i + 1 < argc) impl = argv[++i];
        else if (a == "--runs" && i + 1 < argc) runs = std::max(1, std::atoi(argv[++i]));
        else if (a == "--ops" && i + 1 < argc) ops = (size_t)std::atoll(argv[++i]);
        else if (a == "--no-op-timing") op_timing = false;     // use this when running under perf
        else { std::puts("usage: ./bench --impl A|B|C   [--runs N] [--ops N] [--no-op-timing]"); return 2; }
    }
    if (impl != "A" && impl != "B" && impl != "C") { std::puts("usage: ./bench --impl A|B|C"); return 2; }

    std::vector<Op> w = make_workload(ops, 42);                // fixed seed => identical workload for A, B, C
    double clk = op_timing ? clock_overhead_ns() : 0;
    if (op_timing) std::printf("(clock read overhead %.1f ns is subtracted from per-op numbers)\n", clk);
    if (impl == "A") benchmark<OrderBookA>("A", w, runs, op_timing, clk);
    if (impl == "B") benchmark<OrderBookB>("B", w, runs, op_timing, clk);
    if (impl == "C") benchmark<OrderBookC>("C", w, runs, op_timing, clk);
    return 0;
}