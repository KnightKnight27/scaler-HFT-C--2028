// Single benchmark program for all three order books.
//
//   ./bench --impl A|B|C     benchmark one implementation (5 repetitions, median)
//   ./bench --verify         correctness test: all implementations vs a reference model
//
// See README.md for every option.

#include <malloc.h>
#include <sys/resource.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#define HAVE_TSC 1
#endif

#include "book_array.hpp"
#include "book_map.hpp"
#include "book_vector.hpp"
#include "driver.hpp"

int run_verify(size_t bench_ops, uint32_t seed);  // verify.cpp

namespace {

using SteadyClock = std::chrono::steady_clock;

// --------------------------------------------------------------------------
// Per-operation timer.
//
// Total run time always comes from std::chrono::steady_clock. For per-operation
// latency we need a clock that is much cheaper than the operation itself. On a
// machine whose kernel clocksource is TSC, steady_clock::now() costs ~20 ns, but
// when the kernel has fallen back to HPET (as on the machine these results were
// produced on) every call is a ~1.5 us syscall. The default timer therefore
// reads the invariant TSC directly (~10 ns) and converts ticks to nanoseconds
// using a steady_clock calibration. `--timer steady` uses steady_clock instead.
// --------------------------------------------------------------------------
enum class TimerKind { Tsc, Steady };

struct Timer {
    TimerKind kind = TimerKind::Tsc;
    double ns_per_tick = 1.0;
    double overhead_ticks = 0.0;  // cost of an empty start/stop pair, subtracted

    uint64_t now() const {
#ifdef HAVE_TSC
        if (kind == TimerKind::Tsc) {
            _mm_lfence();  // do not let rdtsc run ahead of earlier instructions
            const uint64_t t = __rdtsc();
            _mm_lfence();
            return t;
        }
#endif
        return static_cast<uint64_t>(SteadyClock::now().time_since_epoch().count());
    }

    double to_ns(double ticks) const { return std::max(0.0, ticks - overhead_ticks) * ns_per_tick; }
};

Timer make_timer(TimerKind kind) {
    Timer t;
#ifndef HAVE_TSC
    kind = TimerKind::Steady;
#endif
    t.kind = kind;
    if (kind == TimerKind::Tsc) {
        // Calibrate TSC ticks against steady_clock over ~200 ms.
        const auto s0 = SteadyClock::now();
        const uint64_t c0 = t.now();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto s1 = SteadyClock::now();
        const uint64_t c1 = t.now();
        t.ns_per_tick = std::chrono::duration<double, std::nano>(s1 - s0).count() / static_cast<double>(c1 - c0);
    } else {
        t.ns_per_tick = std::chrono::duration<double, std::nano>(SteadyClock::duration(1)).count();
    }
    std::vector<uint64_t> pairs(100000);
    for (auto& p : pairs) {
        const uint64_t a = t.now();
        p = t.now() - a;
    }
    std::nth_element(pairs.begin(), pairs.begin() + pairs.size() / 2, pairs.end());
    t.overhead_ticks = static_cast<double>(pairs[pairs.size() / 2]);
    return t;
}

// --------------------------------------------------------------------------
// Results
// --------------------------------------------------------------------------
const char* kOpNames[kNumOpTypes] = {"add", "cancel", "bbo", "depth"};

struct Percentiles {
    double p50, p90, p99, p999, max;
};

struct RunResult {
    double total_ms = 0;              // untimed-per-op pass, steady_clock
    double avg_ns[kNumOpTypes] = {};  // per-op timed pass
    Percentiles pct[kNumOpTypes] = {};
    long minor_faults = 0;            // page faults while building the book (throughput pass)
    size_t heap_bytes = 0;            // heap held by the book at the end of the workload
    double bbo_loop_ns = 0;           // get_bbo() in a tight loop on the final book
    uint64_t checksum = 0;
    BBO final_bbo{};
};

long minor_faults_now() {
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_minflt;
}

size_t heap_in_use() {
    const struct mallinfo2 mi = mallinfo2();
    return mi.uordblks + mi.hblkhd;  // small-chunk bytes in use + mmap'd large blocks
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// Pass 1: run the whole workload with no per-op timers; measures total time,
// page faults and memory. Pass 2: run it again on a fresh book timing every op.
template <class Book>
RunResult run_once(const Workload& w, const Timer& timer, bool latency) {
    RunResult r;

    {
        const size_t heap0 = heap_in_use();
        const long faults0 = minor_faults_now();
        const auto t0 = SteadyClock::now();

        auto book = std::make_unique<Book>();
        Checksum cs;
        for (const Op& op : w.ops) execute(*book, op, cs);

        const auto t1 = SteadyClock::now();
        r.minor_faults = minor_faults_now() - faults0;
        r.heap_bytes = heap_in_use() - heap0;
        r.total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        r.checksum = cs.h;
        r.final_bbo = book->get_bbo();

        // BBO read throughput: the per-op timer cannot resolve a ~3 ns call, so also
        // time a long run of back-to-back calls. get_bbo() lives in another
        // translation unit, so the compiler cannot hoist it out of the loop.
        // Skipped with --no-latency so perf profiles only the workload itself.
        const int kBboLoop = latency ? 10000000 : 0;
        long sink = 0;
        const auto b0 = SteadyClock::now();
        for (int i = 0; i < kBboLoop; ++i) sink += book->get_bbo().bid_qty;
        const auto b1 = SteadyClock::now();
        if (kBboLoop) r.bbo_loop_ns = std::chrono::duration<double, std::nano>(b1 - b0).count() / kBboLoop;
        if (sink == 42) std::puts("");  // keeps `sink` (and the loop) alive
    }

    if (latency) {
        std::vector<uint32_t> samples[kNumOpTypes];
        for (int t = 0; t < kNumOpTypes; ++t) samples[t].reserve(w.count[t]);

        auto book = std::make_unique<Book>();
        Checksum cs;
        for (const Op& op : w.ops) {
            const uint64_t a = timer.now();
            execute(*book, op, cs);
            const uint64_t b = timer.now();
            samples[static_cast<int>(op.type)].push_back(static_cast<uint32_t>(std::min<uint64_t>(b - a, UINT32_MAX)));
        }
        if (cs.h != r.checksum) {
            std::fprintf(stderr, "internal error: checksum differs between passes\n");
            std::exit(2);
        }

        for (int t = 0; t < kNumOpTypes; ++t) {
            auto& s = samples[t];
            if (s.empty()) continue;
            double sum = 0;
            for (uint32_t x : s) sum += x;
            r.avg_ns[t] = timer.to_ns(sum / static_cast<double>(s.size()));
            std::sort(s.begin(), s.end());
            auto at = [&](double q) { return timer.to_ns(s[std::min(s.size() - 1, static_cast<size_t>(q * s.size()))]); };
            r.pct[t] = {at(0.50), at(0.90), at(0.99), at(0.999), timer.to_ns(s.back())};
        }
    }
    return r;
}

void print_with_commas(long v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%ld", v < 0 ? -v : v);
    std::string s = buf, out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i && (s.size() - i) % 3 == 0) out += ',';
        out += s[i];
    }
    std::printf("%s%s", v < 0 ? "-" : "", out.c_str());
}

template <class Book>
int benchmark(const Workload& w, int reps, TimerKind timer_kind, bool latency) {
    std::printf("Implementation %s\n", Book::kName);
    std::printf("Workload: %zu core ops (seed fixed) = %zu add, %zu cancel, %zu bbo; plus %zu x [get_bids(10) + get_asks(10)]\n",
                w.count[0] + w.count[1] + w.count[2], w.count[0], w.count[1], w.count[2], w.count[3]);
    std::printf("Peak live orders: %zu\n", w.peak_live);

    const Timer timer = latency ? make_timer(timer_kind) : Timer{};
    if (latency)
        std::printf("Per-op timer: %s (empty start/stop pair = %.1f ns, subtracted)\n",
                    timer.kind == TimerKind::Tsc ? "TSC, calibrated against steady_clock" : "std::chrono::steady_clock",
                    timer.overhead_ticks * timer.ns_per_tick);
    std::printf("Repetitions: %d (median reported)\n\n", reps);

    std::vector<RunResult> runs;
    std::printf("%-6s %10s %9s %10s %8s %10s %12s %12s %10s\n", "run", "total_ms", "add_ns", "cancel_ns", "bbo_ns",
                "depth_ns", "bbo_loop_ns", "page_faults", "heap_MB");
    for (int i = 0; i < reps; ++i) {
        runs.push_back(run_once<Book>(w, timer, latency));
        const RunResult& r = runs.back();
        std::printf("%-6d %10.2f %9.1f %10.1f %8.1f %10.1f %12.2f %12ld %10.1f\n", i + 1, r.total_ms, r.avg_ns[0],
                    r.avg_ns[1], r.avg_ns[2], r.avg_ns[3], r.bbo_loop_ns, r.minor_faults, r.heap_bytes / 1048576.0);
        if (r.checksum != runs.front().checksum) {
            std::fprintf(stderr, "internal error: checksum changed between repetitions\n");
            return 2;
        }
    }

    auto med = [&](auto field) {
        std::vector<double> v;
        for (const RunResult& r : runs) v.push_back(field(r));
        return median(v);
    };
    std::printf("%-6s %10.2f %9.1f %10.1f %8.1f %10.1f %12.2f %12.0f %10.1f\n", "median",
                med([](const RunResult& r) { return r.total_ms; }),
                med([](const RunResult& r) { return r.avg_ns[0]; }),
                med([](const RunResult& r) { return r.avg_ns[1]; }),
                med([](const RunResult& r) { return r.avg_ns[2]; }),
                med([](const RunResult& r) { return r.avg_ns[3]; }),
                med([](const RunResult& r) { return r.bbo_loop_ns; }),
                med([](const RunResult& r) { return static_cast<double>(r.minor_faults); }),
                med([](const RunResult& r) { return r.heap_bytes / 1048576.0; }));

    std::printf("\nPage faults while building the book on a fresh heap (run 1): %ld\n"
                "(later runs reuse memory the allocator kept from run 1, so they can show 0)\n",
                runs.front().minor_faults);

    if (latency) {
        // Percentiles from the run whose total time is the median.
        std::vector<size_t> order(runs.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return runs[a].total_ms < runs[b].total_ms; });
        const RunResult& m = runs[order[order.size() / 2]];
        std::printf("\nLatency distribution, ns (median run):\n");
        std::printf("%-8s %8s %8s %8s %8s %10s\n", "op", "p50", "p90", "p99", "p99.9", "max");
        for (int t = 0; t < kNumOpTypes; ++t)
            std::printf("%-8s %8.0f %8.0f %8.0f %8.0f %10.0f\n", kOpNames[t], m.pct[t].p50, m.pct[t].p90, m.pct[t].p99,
                        m.pct[t].p999, m.pct[t].max);
    }

    const BBO& b = runs.front().final_bbo;
    std::printf("\nFinal BBO:\n");
    if (b.has_bid) { std::printf("Bid: %.2f x ", b.bid_price); print_with_commas(b.bid_qty); std::printf("\n"); }
    else std::printf("Bid: (none)\n");
    if (b.has_ask) { std::printf("Ask: %.2f x ", b.ask_price); print_with_commas(b.ask_qty); std::printf("\n"); }
    else std::printf("Ask: (none)\n");
    std::printf("\nChecksum: %016llx\n", static_cast<unsigned long long>(runs.front().checksum));
    return 0;
}

void usage() {
    std::puts(
        "usage: bench --impl A|B|C|none [--reps N] [--ops N] [--seed S] [--timer tsc|steady] [--no-latency]\n"
        "       bench --verify [--ops N] [--seed S]\n"
        "\n"
        "  --impl X       A = std::map + std::unordered_map\n"
        "                 B = sorted std::vector levels + id-sorted std::vector orders\n"
        "                 C = tick-indexed array + bitmap + open-addressing order table\n"
        "                 none = only generate the workload (perf baseline for harness cost)\n"
        "  --reps N       repetitions (default 5, median reported)\n"
        "  --ops N        core operations in the workload (default 1000000)\n"
        "  --seed S       RNG seed (default 42)\n"
        "  --timer T      per-op timer: tsc (default on x86) or steady (std::chrono::steady_clock)\n"
        "  --no-latency   skip the per-op timed pass and BBO loop (total time only; use under perf)\n"
        "  --verify       run the correctness test over all implementations");
}

}  // namespace

int main(int argc, char** argv) {
    std::string impl;
    bool verify = false, latency = true;
    int reps = 5;
    size_t ops = 1000000;
    uint32_t seed = 42;
    TimerKind timer = TimerKind::Tsc;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) { usage(); std::exit(1); }
            return argv[++i];
        };
        if (a == "--impl") impl = next();
        else if (a == "--verify") verify = true;
        else if (a == "--reps") reps = std::max(1, std::atoi(next()));
        else if (a == "--ops") ops = std::strtoull(next(), nullptr, 10);
        else if (a == "--seed") seed = static_cast<uint32_t>(std::strtoul(next(), nullptr, 10));
        else if (a == "--timer") {
            const std::string t = next();
            if (t == "tsc") timer = TimerKind::Tsc;
            else if (t == "steady") timer = TimerKind::Steady;
            else { usage(); return 1; }
        }
        else if (a == "--no-latency") latency = false;
        else { usage(); return a == "--help" || a == "-h" ? 0 : 1; }
    }

    if (verify) return run_verify(ops, seed);

    if (impl != "A" && impl != "B" && impl != "C" && impl != "none") { usage(); return 1; }
    const auto g0 = SteadyClock::now();
    const Workload w = make_benchmark_workload(ops, seed);
    if (impl == "none") {
        std::printf("Generated %zu ops in %.2f ms (no book run)\n", w.ops.size(),
                    std::chrono::duration<double, std::milli>(SteadyClock::now() - g0).count());
        return 0;
    }
    if (impl == "A") return benchmark<OrderBookMap>(w, reps, timer, latency);
    if (impl == "B") return benchmark<OrderBookVector>(w, reps, timer, latency);
    return benchmark<OrderBookArray>(w, reps, timer, latency);
}
