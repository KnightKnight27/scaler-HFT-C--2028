// bench.cpp - one benchmark program for all three order books.
//
//   ./bench --impl A|B|C [--runs N] [--ops N] [--seed S] [--ids sequential|scrambled|strided] [--no-latency]
//   ./bench --verify
//   ./bench --baseline        (only builds the workload; fixed cost for perf comparisons)
//
// Build: g++ -std=c++17 -O2 -o bench bench.cpp
//
// Each run uses a fresh book and replays the same pre-generated workload twice:
//   1. throughput pass: no timers inside the loop, gives the total time
//   2. latency pass: every call is wrapped in steady_clock::now(), gives ns per op type
// --no-latency skips pass 2 (used under perf so the counters only see order-book work).
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifdef __linux__
#include <unistd.h>
#endif

#include "array_book.h"
#include "common.h"
#include "map_book.h"
#include "vector_book.h"
#include "verify.h"
#include "workload.h"

namespace {

using Clock = std::chrono::steady_clock;

// Every book gets the same capacity hint: more orders than 1M ops can ever leave resting,
// so no implementation rehashes in the middle of a run.
constexpr size_t kExpectedOrders = 1 << 20;

// Tells the compiler `v` is used, so it cannot delete or move the work that produced it.
template <class T>
inline void keep(const T& v) {
    asm volatile("" : : "r,m"(v) : "memory");
}

inline int64_t now_ns() { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(); }

// Folds query results into one number. The value is printed at the end, so the
// queries cannot be optimised away, and equal checksums across books is a free cross-check.
struct Checksum {
    uint64_t value = 0;
    void add(uint64_t x) { value = value * 1'000'003 + x; }
    void add(const BBO& b) {
        add(b.has_bid ? static_cast<uint64_t>(b.bid_price) * 1'000'000 + b.bid_qty : 1);
        add(b.has_ask ? static_cast<uint64_t>(b.ask_price) * 1'000'000 + b.ask_qty : 2);
    }
    void add(const std::vector<Level>& levels) {
        for (const Level& l : levels) add(static_cast<uint64_t>(l.price) * 1'000'000 + l.qty);
    }
};

struct Latencies {
    std::vector<uint32_t> add, cancel, bbo, depth;  // ns per call
};

struct RunResult {
    double total_ms = 0;
    double avg_add = 0, avg_cancel = 0, avg_bbo = 0, avg_depth = 0;
    double bbo_loop_ns = 0;  // get_bbo in a tight loop, no per-call timer
    uint32_t p50[4] = {}, p99[4] = {}, p999[4] = {}, max[4] = {};
    uint64_t checksum = 0;
    BBO final_bbo;
    size_t final_bid_levels = 0, final_ask_levels = 0;
    long book_rss_kb = -1;
};

long rss_kb() {
#ifdef __linux__
    long pages_total = 0, pages_resident = 0;
    if (FILE* f = std::fopen("/proc/self/statm", "r")) {
        if (std::fscanf(f, "%ld %ld", &pages_total, &pages_resident) != 2) pages_resident = 0;
        std::fclose(f);
    }
    return pages_resident * (sysconf(_SC_PAGESIZE) / 1024);
#else
    return -1;
#endif
}

// Replays the workload. kTimed = wrap each call in a pair of clock reads.
template <class Book, bool kTimed>
void replay(Book& book, const Workload& w, Checksum& sum, Latencies* lat) {
    const size_t n = w.ops.size();
    for (size_t i = 0; i < n; ++i) {
        const Op& op = w.ops[i];
        int64_t t0 = 0;
        if (kTimed) t0 = now_ns();
        switch (op.type) {
            case OpType::Add: {
                bool ok = book.add_order(op.id, op.side, op.price, op.qty);
                keep(ok);
                if (kTimed) lat->add.push_back(static_cast<uint32_t>(now_ns() - t0));
                sum.add(ok);
                break;
            }
            case OpType::Cancel: {
                bool ok = book.cancel_order(op.id);
                keep(ok);
                if (kTimed) lat->cancel.push_back(static_cast<uint32_t>(now_ns() - t0));
                sum.add(ok);
                break;
            }
            case OpType::Bbo: {
                BBO b = book.get_bbo();
                keep(b);
                if (kTimed) lat->bbo.push_back(static_cast<uint32_t>(now_ns() - t0));
                sum.add(b);
                break;
            }
        }

        if (i % kDepthEvery == kDepthEvery - 1) {
            if (kTimed) t0 = now_ns();
            std::vector<Level> bids = book.get_bids(10);
            std::vector<Level> asks = book.get_asks(10);
            keep(bids);
            keep(asks);
            if (kTimed) lat->depth.push_back(static_cast<uint32_t>(now_ns() - t0));
            sum.add(bids);
            sum.add(asks);
        }
    }
}

double average(const std::vector<uint32_t>& v) {
    if (v.empty()) return 0;
    double s = 0;
    for (uint32_t x : v) s += x;
    return s / static_cast<double>(v.size());
}

uint32_t percentile(std::vector<uint32_t>& v, double p) {
    if (v.empty()) return 0;
    size_t k = static_cast<size_t>(p * static_cast<double>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

template <class Book>
RunResult run_once(const Workload& w, bool measure_latency, bool measure_rss) {
    RunResult r;

    // pass 1: total time, nothing but the book calls inside the loop
    {
        const long rss_before = measure_rss ? rss_kb() : -1;
        auto book = std::make_unique<Book>(kExpectedOrders);
        Checksum sum;
        const auto t0 = Clock::now();
        replay<Book, false>(*book, w, sum, nullptr);
        const auto t1 = Clock::now();
        r.total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        r.checksum = sum.value;
        r.final_bbo = book->get_bbo();
        r.final_bid_levels = book->get_bids(kLevelsPerSide).size();
        r.final_ask_levels = book->get_asks(kLevelsPerSide).size();
        if (measure_rss && rss_before >= 0) r.book_rss_kb = rss_kb() - rss_before;

        // get_bbo is only a few ns, too close to the cost of the clock reads to see it in
        // pass 2, so also time many back-to-back calls on the full book with one clock pair.
        if (measure_latency) {
            constexpr int kCalls = 10'000'000;
            const auto b0 = Clock::now();
            for (int k = 0; k < kCalls; ++k) {
                BBO b = book->get_bbo();
                keep(b);  // the "memory" clobber also forces every call to re-read the book
            }
            r.bbo_loop_ns = std::chrono::duration<double, std::nano>(Clock::now() - b0).count() / kCalls;
        }
    }

    // pass 2: per-operation latency
    if (measure_latency) {
        Latencies lat;
        lat.add.reserve(w.adds);
        lat.cancel.reserve(w.cancels);
        lat.bbo.reserve(w.bbos);
        lat.depth.reserve(w.ops.size() / kDepthEvery + 1);
        auto book = std::make_unique<Book>(kExpectedOrders);
        Checksum sum;
        replay<Book, true>(*book, w, sum, &lat);
        if (sum.value != r.checksum) {
            std::fprintf(stderr, "checksum differs between passes - the book is not deterministic!\n");
            std::exit(2);
        }
        r.avg_add = average(lat.add);
        r.avg_cancel = average(lat.cancel);
        r.avg_bbo = average(lat.bbo);
        r.avg_depth = average(lat.depth);
        std::vector<uint32_t>* all[4] = {&lat.add, &lat.cancel, &lat.bbo, &lat.depth};
        for (int k = 0; k < 4; ++k) {
            r.p50[k] = percentile(*all[k], 0.50);
            r.p99[k] = percentile(*all[k], 0.99);
            r.p999[k] = percentile(*all[k], 0.999);
            r.max[k] = all[k]->empty() ? 0 : *std::max_element(all[k]->begin(), all[k]->end());
        }
    }
    return r;
}

// Cost of one now_ns() pair, which is included in every per-op number.
double timer_overhead_ns() {
    std::vector<uint32_t> d(200'000);
    for (auto& x : d) {
        int64_t a = now_ns();
        int64_t b = now_ns();
        x = static_cast<uint32_t>(b - a);
    }
    return percentile(d, 0.5);
}

template <class T, class F>
T median_of(std::vector<RunResult>& runs, F field) {
    std::vector<T> v;
    for (auto& r : runs) v.push_back(field(r));
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

template <class Book>
int bench(const Workload& w, const WorkloadConfig& cfg, int runs, bool measure_latency) {
    std::printf("Implementation %s\n", Book::kName);
    const char* id_names[] = {"sequential", "scrambled", "strided"};
    std::printf("workload: %zu ops (seed %u, %s ids): %zu add, %zu cancel, %zu get_bbo, + get_bids(10)/get_asks(10) every %zu ops\n",
                w.ops.size(), cfg.seed, id_names[static_cast<int>(cfg.ids)], w.adds, w.cancels, w.bbos, kDepthEvery);
    if (measure_latency)
        std::printf("timer overhead: ~%.0f ns per steady_clock pair (included in the per-op numbers)\n", timer_overhead_ns());
    std::printf("\n");

    std::vector<RunResult> results;
    for (int i = 0; i < runs; ++i) {
        results.push_back(run_once<Book>(w, measure_latency, i == 0));
        const RunResult& r = results.back();
        if (measure_latency) {
            std::printf("run %d  total %7.1f ms | add %6.1f ns | cancel %6.1f ns | bbo %5.1f ns | depth %6.1f ns\n", i + 1,
                        r.total_ms, r.avg_add, r.avg_cancel, r.avg_bbo, r.avg_depth);
        } else {
            std::printf("run %d  total %7.1f ms\n", i + 1, r.total_ms);
        }
    }

    const double total = median_of<double>(results, [](const RunResult& r) { return r.total_ms; });
    std::printf("\nMEDIAN of %d runs\n", runs);
    std::printf("  total time (throughput pass): %.1f ms  = %.1f ns per op\n", total,
                total * 1e6 / static_cast<double>(w.ops.size()));
    if (measure_latency) {
        const char* names[4] = {"add_order", "cancel_order", "get_bbo", "get_bids+get_asks(10)"};
        double (*avg[4])(const RunResult&) = {
            [](const RunResult& r) { return r.avg_add; }, [](const RunResult& r) { return r.avg_cancel; },
            [](const RunResult& r) { return r.avg_bbo; }, [](const RunResult& r) { return r.avg_depth; }};
        std::printf("  %-22s %8s %7s %7s %7s %8s\n", "ns per call", "avg", "p50", "p99", "p99.9", "max");
        for (int k = 0; k < 4; ++k) {
            std::printf("  %-22s %8.1f %7u %7u %7u %8u\n", names[k], median_of<double>(results, avg[k]),
                        median_of<uint32_t>(results, [k](const RunResult& r) { return r.p50[k]; }),
                        median_of<uint32_t>(results, [k](const RunResult& r) { return r.p99[k]; }),
                        median_of<uint32_t>(results, [k](const RunResult& r) { return r.p999[k]; }),
                        median_of<uint32_t>(results, [k](const RunResult& r) { return r.max[k]; }));
        }
        std::printf("  get_bbo back-to-back (10M calls on the final book, one clock pair): %.2f ns per call\n",
                    median_of<double>(results, [](const RunResult& r) { return r.bbo_loop_ns; }));
    }

    const RunResult& last = results.back();
    std::printf("\nbook at the end: %zu bid levels, %zu ask levels\n", last.final_bid_levels, last.final_ask_levels);
    if (results.front().book_rss_kb >= 0)
        std::printf("memory: resident set grew by %.1f MB while the book was built (run 1)\n",
                    results.front().book_rss_kb / 1024.0);

    const BBO& b = last.final_bbo;
    std::printf("\nFinal BBO:\n");
    if (b.has_bid) std::printf("Bid: %s x %lld\n", format_price(b.bid_price).c_str(), static_cast<long long>(b.bid_qty));
    else std::printf("Bid: none\n");
    if (b.has_ask) std::printf("Ask: %s x %lld\n", format_price(b.ask_price).c_str(), static_cast<long long>(b.ask_qty));
    else std::printf("Ask: none\n");
    std::printf("\nChecksum: %llu\n", static_cast<unsigned long long>(last.checksum));
    return 0;
}

void usage() {
    std::printf(
        "usage: bench --impl A|B|C [--runs N] [--ops N] [--seed S] [--no-latency]\n"
        "       bench --verify\n"
        "       bench --baseline [--ops N] [--seed S]\n");
}

}  // namespace

int main(int argc, char** argv) {
    char impl = 0;
    bool verify = false, baseline = false, measure_latency = true;
    int runs = 5;
    WorkloadConfig cfg;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                usage();
                std::exit(1);
            }
            return argv[++i];
        };
        if (a == "--impl") impl = static_cast<char>(std::toupper(static_cast<unsigned char>(next()[0])));
        else if (a == "--runs") runs = std::max(1, std::atoi(next()));
        else if (a == "--ops") cfg.num_ops = static_cast<size_t>(std::atoll(next()));
        else if (a == "--seed") cfg.seed = static_cast<uint32_t>(std::atoll(next()));
        else if (a == "--ids") {
            const std::string v = next();
            if (v == "sequential") cfg.ids = IdPattern::Sequential;
            else if (v == "scrambled") cfg.ids = IdPattern::Scrambled;
            else if (v == "strided") cfg.ids = IdPattern::Strided;
            else {
                usage();
                return 1;
            }
        }
        else if (a == "--no-latency") measure_latency = false;
        else if (a == "--verify") verify = true;
        else if (a == "--baseline") baseline = true;
        else {
            usage();
            return 1;
        }
    }

    if (verify) return run_verify();

    const Workload w = make_workload(cfg);
    if (baseline) {
        Checksum sum;
        for (const Op& op : w.ops) sum.add(op.id ^ static_cast<uint64_t>(op.price));
        std::printf("baseline: generated %zu ops, checksum %llu\n", w.ops.size(), static_cast<unsigned long long>(sum.value));
        return 0;
    }

    switch (impl) {
        case 'A': return bench<MapOrderBook>(w, cfg, runs, measure_latency);
        case 'B': return bench<VectorOrderBook>(w, cfg, runs, measure_latency);
        case 'C': return bench<ArrayOrderBook>(w, cfg, runs, measure_latency);
        default: usage(); return 1;
    }
}
