// Benchmark + correctness test for the three order books.
//
//   ./bench --verify          correctness test, A, B and C vs a brute force book (verify.hpp)
//   ./bench --impl A|B|C      benchmark one implementation (5 runs, median)
//
// extra flags: --runs N, --ops N, --seed S, --no-timers (only total time)

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "array_book.hpp"
#include "heap_book.hpp"
#include "map_book.hpp"
#include "verify.hpp"
#include "workload.hpp"

using Clock = std::chrono::steady_clock;

// results of the timed pass end up here so the compiler has to keep them
volatile uint64_t g_sink;

// --no-timers skips pass 2 (see run_once) and the clock overhead measurement.
// We added it after perf record showed clock_gettime at about a third of C's
// samples, which was messing up the counters.
bool g_op_timers = true;

// One get_bbo() is quicker than one steady_clock::now() call, so timing a
// single call only measures the clock. For BBO ops we time this many calls
// back to back and divide.
constexpr int BBO_BATCH = 16;

struct RunResult {
    double total_ms = 0;
    double add_ns = 0, cancel_ns = 0, bbo_ns = 0;  // raw, still include the clock overhead
    uint64_t checksum = 0;
};

// rough cost of one steady_clock::now() call, so we can subtract it from the
// per-op numbers
double timer_overhead_ns() {
    const int n = 1000000;
    uint64_t sink = 0;
    auto a = Clock::now();
    for (int i = 0; i < n; i++) sink += Clock::now().time_since_epoch().count();
    auto b = Clock::now();
    g_sink = sink;
    return std::chrono::duration<double, std::nano>(b - a).count() / n;
}

double ns_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::nano>(b - a).count();
}

// Pass 1: play the whole workload with no timers in the loop, for the total
// time and the checksum.
// Pass 2: play it again on a fresh book with steady_clock::now() around every
// add/cancel/bbo, for the per-op averages.
// (one pass with the timers in it would make the total time mostly clock calls)
template <class Book>
RunResult run_once(const std::vector<Op>& ops) {
    RunResult r;
    {
        auto book = std::make_unique<Book>();
        auto start = Clock::now();
        r.checksum = play(*book, ops);
        auto end = Clock::now();
        r.total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    }
    if (!g_op_timers) return r;

    auto book = std::make_unique<Book>();
    double add_ns = 0, cancel_ns = 0, bbo_ns = 0;
    size_t adds = 0, cancels = 0, bbos = 0;
    uint64_t sink = 0;
    double price_sum = 0;
    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        if (op.type == OpType::Add) {
            auto t0 = Clock::now();
            book->add_order(op.id, op.side, op.price, op.qty);
            auto t1 = Clock::now();
            add_ns += ns_between(t0, t1);
            adds++;
        } else if (op.type == OpType::Cancel) {
            auto t0 = Clock::now();
            bool ok = book->cancel_order(op.id);
            auto t1 = Clock::now();
            sink += ok;
            cancel_ns += ns_between(t0, t1);
            cancels++;
        } else {
            BBO out[BBO_BATCH];
            auto t0 = Clock::now();
            for (int k = 0; k < BBO_BATCH; k++) {
                out[k] = book->get_bbo();
                // compiler-only barrier (emits no instruction), stops the
                // compiler from merging the calls into one
                std::atomic_signal_fence(std::memory_order_seq_cst);
            }
            auto t1 = Clock::now();
            // use every field (prices too), otherwise the compiler skips
            // computing them inside the timed loop
            for (const BBO& b : out) {
                sink += b.bid_qty + b.ask_qty;
                price_sum += b.bid_price + b.ask_price;
            }
            bbo_ns += ns_between(t0, t1) / BBO_BATCH;
            bbos++;
        }
        if ((i + 1) % 100 == 0) {
            sink += book->get_bids(10).size();
            sink += book->get_asks(10).size();
        }
    }
    g_sink = sink + (uint64_t)price_sum;
    r.add_ns = adds ? add_ns / adds : 0;
    r.cancel_ns = cancels ? cancel_ns / cancels : 0;
    r.bbo_ns = bbos ? bbo_ns / bbos : 0;
    return r;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

template <class Book>
int run_bench(const char* name, size_t n_ops, uint32_t seed, int runs) {
    std::vector<Op> ops = make_workload(n_ops, seed);
    WorkloadCounts c = count_ops(ops);
    double overhead = g_op_timers ? timer_overhead_ns() : 0;

    std::printf("Implementation %s\n", name);
    std::printf("ops: %zu (add %zu, cancel %zu, bbo %zu), seed %u, runs %d\n", ops.size(), c.add,
                c.cancel, c.bbo, seed, runs);
    if (g_op_timers) std::printf("steady_clock::now() overhead: %.1f ns\n", overhead);
    std::printf("\n");

    std::vector<double> total, add, cancel, bbo;
    uint64_t checksum = 0;
    for (int k = 0; k < runs; k++) {
        RunResult r = run_once<Book>(ops);
        if (g_op_timers)
            std::printf("run %d: total %8.2f ms | raw add %6.1f ns | cancel %6.1f ns | bbo %5.2f ns\n", k + 1,
                        r.total_ms, r.add_ns, r.cancel_ns, r.bbo_ns);
        else
            std::printf("run %d: total %8.2f ms\n", k + 1, r.total_ms);
        total.push_back(r.total_ms);
        add.push_back(r.add_ns);
        cancel.push_back(r.cancel_ns);
        bbo.push_back(r.bbo_ns);
        if (k > 0 && r.checksum != checksum) {
            std::printf("checksum changed between runs?!\n");
            return 1;
        }
        checksum = r.checksum;
    }

    std::printf("\nmedian of %d runs:\n", runs);
    std::printf("  total time   %8.2f ms  (%.1f ns per op, untimed loop)\n", median(total),
                median(total) * 1e6 / ops.size());
    if (g_op_timers) {
        // raw = what we measured, net = raw minus the clock overhead
        // (for bbo the overhead is shared by the BBO_BATCH calls)
        std::printf("  ns / add     %8.1f  (raw %.1f)\n", std::max(0.0, median(add) - overhead), median(add));
        std::printf("  ns / cancel  %8.1f  (raw %.1f)\n", std::max(0.0, median(cancel) - overhead),
                    median(cancel));
        std::printf("  ns / bbo     %8.2f  (raw %.2f, timed in batches of %d calls)\n",
                    std::max(0.0, median(bbo) - overhead / BBO_BATCH), median(bbo), BBO_BATCH);
    }

    // play once more to print the final state of the book
    auto book = std::make_unique<Book>();
    play(*book, ops);
    BBO b = book->get_bbo();
    std::printf("\nFinal BBO:\n");
    if (b.has_bid) std::printf("Bid: %.2f x %ld\n", b.bid_price, b.bid_qty);
    else std::printf("Bid: none\n");
    if (b.has_ask) std::printf("Ask: %.2f x %ld\n", b.ask_price, b.ask_qty);
    else std::printf("Ask: none\n");
    std::printf("Live orders: %zu\n", book->order_count());
    std::printf("\nChecksum: %llu\n", (unsigned long long)checksum);
    return 0;
}

void usage() {
    std::printf("usage: ./bench --impl A|B|C [--runs N] [--ops N] [--seed S] [--no-timers]\n");
    std::printf("       ./bench --verify [--ops N] [--seed S]\n");
}

// the whole string has to be a number between lo and hi, so things like
// "-1", "1e6" or "abc" get rejected instead of half parsed
bool read_num(const char* s, unsigned long long lo, unsigned long long hi, unsigned long long& out) {
    if (!std::isdigit((unsigned char)s[0])) return false;
    char* end;
    errno = 0;
    out = std::strtoull(s, &end, 10);
    return *end == '\0' && errno == 0 && out >= lo && out <= hi;
}

int main(int argc, char** argv) {
    std::string impl;
    bool verify = false;
    int runs = 5;
    size_t ops = 0;  // 0 = not given
    uint32_t seed = 0;
    bool seed_set = false;
    unsigned long long n = 0;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        bool has_val = i + 1 < argc;
        if (a == "--verify") verify = true;
        else if (a == "--no-timers") g_op_timers = false;
        else if (a == "--impl" && has_val) impl = argv[++i];
        else if (a == "--runs" && has_val && read_num(argv[++i], 1, 1000, n)) runs = (int)n;
        else if (a == "--ops" && has_val && read_num(argv[++i], 1, 10000000, n)) ops = n;
        else if (a == "--seed" && has_val && read_num(argv[++i], 0, 4294967295ULL, n)) {
            seed = (uint32_t)n;
            seed_set = true;
        }
        else { usage(); return 1; }
    }

    if (verify) return run_verify(ops ? ops : 200000, seed_set ? seed : 7);

    if (ops == 0) ops = 1000000;
    if (!seed_set) seed = 42;
    for (char& ch : impl) ch = (char)std::toupper((unsigned char)ch);
    if (impl == "A") return run_bench<MapBook>("A (std::map + unordered_map)", ops, seed, runs);
    if (impl == "B") return run_bench<HeapBook>("B (priority_queue + lazy delete)", ops, seed, runs);
    if (impl == "C") return run_bench<ArrayBook>("C (flat array + bitmap + own hash table)", ops, seed, runs);
    usage();
    return 1;
}
