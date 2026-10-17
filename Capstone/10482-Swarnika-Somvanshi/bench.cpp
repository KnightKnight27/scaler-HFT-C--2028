// bench.cpp
// One benchmark program for all three order books.
// Usage: ./bench --impl A|B|C [--runs N]
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "common.h"
#include "implem_a.h"
#include "implem_b.h"
#include "implem_c.h"
#include "workload.h"

using Clock = std::chrono::steady_clock;

struct OpStats {
    double avg_ns = 0;
    double p99_ns = 0;
};

struct RunResult {
    double total_ms = 0;
    OpStats add, cancel, bbo;
    unsigned long long checksum = 0;
    BBO final_bbo{};
};

// average and p99 of a list of latencies (in ns)
static OpStats make_stats(std::vector<uint32_t>& v) {
    OpStats s;
    if (v.empty()) return s;
    double sum = 0;
    for (uint32_t x : v) sum += x;
    s.avg_ns = sum / v.size();

    size_t idx = static_cast<size_t>(v.size() * 0.99);
    if (idx >= v.size()) idx = v.size() - 1;
    std::nth_element(v.begin(), v.begin() + idx, v.end());
    s.p99_ns = v[idx];
    return s;
}

static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

static uint32_t elapsed_ns(Clock::time_point t0, Clock::time_point t1) {
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
}

// One full pass over the workload with a fresh book
template <typename Book>
RunResult run_once(const std::vector<Op>& ops) {
    auto book = std::make_unique<Book>();

    std::vector<uint32_t> add_ns, cancel_ns, bbo_ns;
    add_ns.reserve(ops.size());
    cancel_ns.reserve(ops.size());
    bbo_ns.reserve(ops.size());

    unsigned long long checksum = 0;

    auto start = Clock::now();

    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];

        if (op.type == OpType::Add) {
            auto t0 = Clock::now();
            book->add_order(op.id, op.side, op.tick, op.qty);
            auto t1 = Clock::now();
            add_ns.push_back(elapsed_ns(t0, t1));
        } else if (op.type == OpType::Cancel) {
            auto t0 = Clock::now();
            bool ok = book->cancel_order(op.id);
            auto t1 = Clock::now();
            cancel_ns.push_back(elapsed_ns(t0, t1));
            checksum += ok ? 1 : 0;
        } else {
            auto t0 = Clock::now();
            BBO b = book->get_bbo();
            auto t1 = Clock::now();
            bbo_ns.push_back(elapsed_ns(t0, t1));
            checksum += b.bid_tick + b.bid_qty * 3 + b.ask_tick * 5 + b.ask_qty * 7;
        }

        // every 100 ops: top 10 levels on both sides (not timed separately)
        if ((i + 1) % 100 == 0) {
            std::vector<Level> bids = book->get_bids(10);
            std::vector<Level> asks = book->get_asks(10);
            for (const Level& l : bids) checksum += l.tick * 31 + l.qty;
            for (const Level& l : asks) checksum += l.tick * 37 + l.qty;
        }
    }

    auto end = Clock::now();

    RunResult r;
    r.total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    r.add = make_stats(add_ns);
    r.cancel = make_stats(cancel_ns);
    r.bbo = make_stats(bbo_ns);
    r.checksum = checksum;
    r.final_bbo = book->get_bbo();
    return r;
}

// Run `runs` times and print the median of each number
template <typename Book>
void run_benchmark(const char* name, const std::vector<Op>& ops, int runs) {
    std::vector<double> total, add_avg, add_p99, can_avg, can_p99, bbo_avg, bbo_p99;
    RunResult last;

    for (int k = 0; k < runs; ++k) {
        last = run_once<Book>(ops);
        total.push_back(last.total_ms);
        add_avg.push_back(last.add.avg_ns);
        add_p99.push_back(last.add.p99_ns);
        can_avg.push_back(last.cancel.avg_ns);
        can_p99.push_back(last.cancel.p99_ns);
        bbo_avg.push_back(last.bbo.avg_ns);
        bbo_p99.push_back(last.bbo.p99_ns);
    }

    std::printf("%s: MEDIAN total %.1fms | add %.1fns (p99 %.0f) | cancel %.1fns (p99 %.0f) | bbo %.1fns (p99 %.0f)\n",
                name, median(total),
                median(add_avg), median(add_p99),
                median(can_avg), median(can_p99),
                median(bbo_avg), median(bbo_p99));

    // print the final BBO and a checksum so the work can't be optimized away
    const BBO& b = last.final_bbo;
    std::printf("Final BBO:\n");
    if (b.has_bid)
        std::printf("  Bid: %.2f x %ld\n", tick_to_price(b.bid_tick), b.bid_qty);
    else
        std::printf("  Bid: none\n");
    if (b.has_ask)
        std::printf("  Ask: %.2f x %ld\n", tick_to_price(b.ask_tick), b.ask_qty);
    else
        std::printf("  Ask: none\n");
    std::printf("Checksum: %llu\n", last.checksum);
}

int main(int argc, char** argv) {
    const char* impl = nullptr;
    int runs = 5;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--impl") == 0 && i + 1 < argc)
            impl = argv[++i];
        else if (std::strcmp(argv[i], "--runs") == 0 && i + 1 < argc)
            runs = std::atoi(argv[++i]);
    }

    if (!impl || runs < 1 || (std::strcmp(impl, "A") && std::strcmp(impl, "B") && std::strcmp(impl, "C"))) {
        std::printf("Usage: ./bench --impl A|B|C [--runs N]\n");
        return 1;
    }

    // generated once, outside all timing
    std::vector<Op> ops = generate_workload();

    if (impl[0] == 'A') run_benchmark<OrderBookA>("A", ops, runs);
    if (impl[0] == 'B') run_benchmark<OrderBookB>("B", ops, runs);
    if (impl[0] == 'C') run_benchmark<OrderBookC>("C", ops, runs);
    return 0;
}
