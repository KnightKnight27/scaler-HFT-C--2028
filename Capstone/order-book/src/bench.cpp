// Benchmark and correctness driver for the three order books.
//
//   ./bench --verify             correctness tests for all three implementations
//   ./bench --impl A|B|C|all     benchmark, 5 runs per implementation, median reported
//   ./bench --impl A --perf      one plain pass over the workload, for perf stat / perf record
//   ./bench --impl none --perf   only generates the workload (baseline for perf)
//
// Options: --runs N (default 5), --ops N (default 1000000), --seed N (default 42)

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "array_order_book.hpp"
#include "map_order_book.hpp"
#include "platform.hpp"
#include "sorted_vector_order_book.hpp"
#include "verify.hpp"
#include "workload.hpp"

namespace {

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// Checksum

// Folds every result the book returns into one number. Printing it at the end
// stops the compiler from dropping any work, and equal checksums show that two
// books returned exactly the same results.
class Checksum {
public:
    void mix(std::uint64_t value) { hash_ = (hash_ ^ value) * 0x100000001b3ull; }

    void mix(const BBO& bbo) {
        mix(bbo.has_bid);
        if (bbo.has_bid) {
            mix(bits_of(bbo.bid_price));
            mix(static_cast<std::uint64_t>(bbo.bid_qty));
        }
        mix(bbo.has_ask);
        if (bbo.has_ask) {
            mix(bits_of(bbo.ask_price));
            mix(static_cast<std::uint64_t>(bbo.ask_qty));
        }
    }

    void mix(const std::vector<Level>& levels) {
        mix(levels.size());
        for (const Level& level : levels) {
            mix(bits_of(level.price));
            mix(static_cast<std::uint64_t>(level.qty));
        }
    }

    std::uint64_t value() const { return hash_; }

private:
    static std::uint64_t bits_of(double value) {
        std::uint64_t bits;
        std::memcpy(&bits, &value, sizeof bits);
        return bits;
    }

    std::uint64_t hash_ = 0xcbf29ce484222325ull;
};

// ---------------------------------------------------------------------------
// Running the workload

// Runs every operation with no timing inside the loop.
template <class Book>
std::uint64_t run_plain(Book& book, const Workload& workload) {
    Checksum checksum;
    int until_depth = kDepthEvery;
    for (const Op& op : workload.ops) {
        switch (op.type) {
        case OpType::Add:
            checksum.mix(book.add_order(op.id, op.side, op.price, op.qty));
            break;
        case OpType::Cancel:
            checksum.mix(book.cancel_order(op.id));
            break;
        case OpType::Bbo:
            checksum.mix(book.get_bbo());
            break;
        }
        if (--until_depth == 0) {
            until_depth = kDepthEvery;
            checksum.mix(book.get_bids(kDepth));
            checksum.mix(book.get_asks(kDepth));
        }
    }
    return checksum.value();
}

// Nanoseconds taken by each call, one vector per kind of call.
struct Samples {
    std::vector<std::uint32_t> add;
    std::vector<std::uint32_t> cancel;
    std::vector<std::uint32_t> bbo;
    std::vector<std::uint32_t> depth;  // one get_bids(10) plus one get_asks(10)

    void reserve(const Workload& workload) {
        add.reserve(workload.adds);
        cancel.reserve(workload.cancels);
        bbo.reserve(workload.bbo_queries);
        depth.reserve(workload.ops.size() / kDepthEvery + 1);
    }

    void clear() {
        add.clear();
        cancel.clear();
        bbo.clear();
        depth.clear();
    }
};

std::uint32_t nanoseconds(Clock::time_point start, Clock::time_point end) {
    return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
}

// Same operations as run_plain, but reads the clock right before and right
// after every call. The switch on the operation type happens outside the timed
// region, so its branch mispredictions are not counted.
template <class Book>
std::uint64_t run_timed(Book& book, const Workload& workload, Samples& samples) {
    Checksum checksum;
    int until_depth = kDepthEvery;
    for (const Op& op : workload.ops) {
        switch (op.type) {
        case OpType::Add: {
            const auto start = Clock::now();
            const bool added = book.add_order(op.id, op.side, op.price, op.qty);
            const auto end = Clock::now();
            samples.add.push_back(nanoseconds(start, end));
            checksum.mix(added);
            break;
        }
        case OpType::Cancel: {
            const auto start = Clock::now();
            const bool cancelled = book.cancel_order(op.id);
            const auto end = Clock::now();
            samples.cancel.push_back(nanoseconds(start, end));
            checksum.mix(cancelled);
            break;
        }
        case OpType::Bbo: {
            const auto start = Clock::now();
            const BBO bbo = book.get_bbo();
            const auto end = Clock::now();
            samples.bbo.push_back(nanoseconds(start, end));
            checksum.mix(bbo);
            break;
        }
        }
        if (--until_depth == 0) {
            until_depth = kDepthEvery;
            const auto start = Clock::now();
            const std::vector<Level> bids = book.get_bids(kDepth);
            const std::vector<Level> asks = book.get_asks(kDepth);
            const auto end = Clock::now();
            samples.depth.push_back(nanoseconds(start, end));
            checksum.mix(bids);
            checksum.mix(asks);
        }
    }
    return checksum.value();
}

// ---------------------------------------------------------------------------
// Statistics

struct Latency {
    double mean = 0;
    double p50 = 0;
    double p90 = 0;
    double p99 = 0;
    double p999 = 0;
    double max = 0;
};

// Mean and percentiles of the samples, minus the cost of reading the clock.
Latency summarize(std::vector<std::uint32_t>& samples, double timer_ns) {
    Latency latency;
    if (samples.empty()) return latency;
    double total = 0;
    for (const std::uint32_t ns : samples) total += ns;
    auto corrected = [timer_ns](double ns) { return std::max(0.0, ns - timer_ns); };
    auto percentile = [&samples](double fraction) {
        const std::size_t k = std::min(samples.size() - 1, static_cast<std::size_t>(fraction * samples.size()));
        std::nth_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(k), samples.end());
        return static_cast<double>(samples[k]);
    };
    latency.mean = corrected(total / samples.size());
    latency.p50 = corrected(percentile(0.50));
    latency.p90 = corrected(percentile(0.90));
    latency.p99 = corrected(percentile(0.99));
    latency.p999 = corrected(percentile(0.999));
    latency.max = corrected(*std::max_element(samples.begin(), samples.end()));
    return latency;
}

// Average time between two back-to-back steady_clock::now() calls. This is the
// part of every per-call sample that is spent reading the clock.
double timer_overhead_ns() {
    constexpr int kPairs = 1000000;
    double total = 0;
    for (int i = 0; i < kPairs; ++i) {
        const auto start = Clock::now();
        const auto end = Clock::now();
        total += nanoseconds(start, end);
    }
    return total / kPairs;
}

struct RunResult {
    double total_ms = 0;
    double ipc = 0;  // 0 when the OS does not report cycles
    double page_faults = 0;
    double bbo_loop_ns = 0;  // get_bbo timed as kBboLoopCalls calls in a row
    long bbo_loop_sink = 0;  // uses the results of those calls
    Latency add;
    Latency cancel;
    Latency bbo;
    Latency depth;
    std::uint64_t checksum = 0;
    BBO final_bbo{};
};

// get_bbo takes less time than one clock read, so besides timing it per call
// we also time this many calls in a row on the final book.
constexpr int kBboLoopCalls = 1000000;

template <class Field>
double median(const std::vector<RunResult>& runs, Field field) {
    std::vector<double> values;
    for (const RunResult& run : runs) values.push_back(field(run));
    std::sort(values.begin(), values.end());
    const std::size_t n = values.size();
    return n % 2 == 1 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2;
}

Latency median_latency(const std::vector<RunResult>& runs, Latency RunResult::*member) {
    Latency latency;
    latency.mean = median(runs, [member](const RunResult& r) { return (r.*member).mean; });
    latency.p50 = median(runs, [member](const RunResult& r) { return (r.*member).p50; });
    latency.p90 = median(runs, [member](const RunResult& r) { return (r.*member).p90; });
    latency.p99 = median(runs, [member](const RunResult& r) { return (r.*member).p99; });
    latency.p999 = median(runs, [member](const RunResult& r) { return (r.*member).p999; });
    latency.max = median(runs, [member](const RunResult& r) { return (r.*member).max; });
    return latency;
}

// ---------------------------------------------------------------------------
// Benchmarking one implementation

struct Impl {
    char key;
    const char* name;
};

const Impl kImplA{'A', "std::map levels + std::unordered_map orders"};
const Impl kImplB{'B', "sorted vectors + binary search"};
const Impl kImplC{'C', "price-indexed array + bitmap + open-addressing hash table"};

void print_final(const BBO& bbo, std::uint64_t checksum) {
    std::printf("Final BBO:\n");
    if (bbo.has_bid) {
        std::printf("Bid: %.2f x %ld\n", bbo.bid_price, bbo.bid_qty);
    } else {
        std::printf("Bid: none\n");
    }
    if (bbo.has_ask) {
        std::printf("Ask: %.2f x %ld\n", bbo.ask_price, bbo.ask_qty);
    } else {
        std::printf("Ask: none\n");
    }
    std::printf("\nChecksum: 0x%016llx\n", static_cast<unsigned long long>(checksum));
}

template <class Book>
RunResult run_once(const Workload& workload, Samples& samples, double timer_ns) {
    RunResult result;
    {
        // Pass 1: the whole workload with one clock read at each end, so the
        // total time has no per-call timing in it.
        auto book = std::make_unique<Book>(workload.max_live_orders);
        const ProcessCounters before = read_process_counters();
        const auto start = Clock::now();
        result.checksum = run_plain(*book, workload);
        const auto end = Clock::now();
        const ProcessCounters after = read_process_counters();

        result.total_ms = std::chrono::duration<double, std::milli>(end - start).count();
        result.page_faults = static_cast<double>(after.minor_page_faults - before.minor_page_faults);
        if (after.has_cpu_counters && after.cycles > before.cycles) {
            result.ipc = static_cast<double>(after.instructions - before.instructions) /
                         static_cast<double>(after.cycles - before.cycles);
        }
        result.final_bbo = book->get_bbo();

        const auto loop_start = Clock::now();
        for (int i = 0; i < kBboLoopCalls; ++i) {
            const BBO bbo = book->get_bbo();
            result.bbo_loop_sink += bbo.bid_qty + bbo.ask_qty;
        }
        const auto loop_end = Clock::now();
        result.bbo_loop_ns = std::chrono::duration<double, std::nano>(loop_end - loop_start).count() / kBboLoopCalls;
    }
    {
        // Pass 2: the same workload on a fresh book, timing every call.
        auto book = std::make_unique<Book>(workload.max_live_orders);
        samples.clear();
        if (run_timed(*book, workload, samples) != result.checksum) {
            std::fprintf(stderr, "error: the timed pass returned different results from the plain pass\n");
            std::exit(1);
        }
        result.add = summarize(samples.add, timer_ns);
        result.cancel = summarize(samples.cancel, timer_ns);
        result.bbo = summarize(samples.bbo, timer_ns);
        result.depth = summarize(samples.depth, timer_ns);
    }
    return result;
}

// One row of the per-run table.
struct Row {
    double total_ms;
    double add;
    double cancel;
    double bbo;
    double bbo_loop;
    double depth;
    double ipc;
    double page_faults;
};

void print_row_header() {
    std::printf("  %-6s %9s %8s %10s %8s %10s %9s %6s %12s\n", "run", "total ms", "add ns", "cancel ns", "bbo ns",
                "bbo loop", "depth ns", "IPC", "page faults");
}

void print_row(const char* label, const Row& row) {
    char ipc_text[16] = "n/a";
    if (row.ipc > 0) std::snprintf(ipc_text, sizeof ipc_text, "%.2f", row.ipc);
    std::printf("  %-6s %9.2f %8.1f %10.1f %8.1f %10.2f %9.1f %6s %12.0f\n", label, row.total_ms, row.add, row.cancel,
                row.bbo, row.bbo_loop, row.depth, ipc_text, row.page_faults);
}

void print_latency_row(const char* label, const Latency& l) {
    std::printf("  %-8s %8.1f %8.0f %8.0f %8.0f %8.0f %9.0f\n", label, l.mean, l.p50, l.p90, l.p99, l.p999, l.max);
}

struct Summary {
    char key = '?';
    double total_ms = 0;
    double ipc = 0;
    double page_faults = 0;
    double bbo_loop_ns = 0;
    Latency add;
    Latency cancel;
    Latency bbo;
    Latency depth;
    std::uint64_t checksum = 0;
};

// One implementation taking part in the benchmark, with its results so far.
struct Contender {
    Impl impl;
    RunResult (*run_once)(const Workload&, Samples&, double);
    std::vector<RunResult> results;
};

Summary report(const Impl& impl, const std::vector<RunResult>& results) {
    std::printf("\nImplementation %c: %s\n\n", impl.key, impl.name);
    print_row_header();
    for (std::size_t run = 0; run < results.size(); ++run) {
        const RunResult& r = results[run];
        const std::string label = std::to_string(run + 1);
        print_row(label.c_str(), Row{r.total_ms, r.add.mean, r.cancel.mean, r.bbo.mean, r.bbo_loop_ns, r.depth.mean,
                                     r.ipc, r.page_faults});
    }

    Summary summary;
    summary.key = impl.key;
    summary.total_ms = median(results, [](const RunResult& r) { return r.total_ms; });
    summary.ipc = median(results, [](const RunResult& r) { return r.ipc; });
    summary.page_faults = median(results, [](const RunResult& r) { return r.page_faults; });
    summary.bbo_loop_ns = median(results, [](const RunResult& r) { return r.bbo_loop_ns; });
    summary.add = median_latency(results, &RunResult::add);
    summary.cancel = median_latency(results, &RunResult::cancel);
    summary.bbo = median_latency(results, &RunResult::bbo);
    summary.depth = median_latency(results, &RunResult::depth);
    summary.checksum = results.front().checksum;
    print_row("median", Row{summary.total_ms, summary.add.mean, summary.cancel.mean, summary.bbo.mean,
                            summary.bbo_loop_ns, summary.depth.mean, summary.ipc, summary.page_faults});

    std::printf("\n  Latency per call in ns (median over runs, clock overhead removed)\n");
    std::printf("  %-8s %8s %8s %8s %8s %8s %9s\n", "", "mean", "p50", "p90", "p99", "p99.9", "max");
    print_latency_row("add", summary.add);
    print_latency_row("cancel", summary.cancel);
    print_latency_row("get_bbo", summary.bbo);
    print_latency_row("depth", summary.depth);
    std::printf("  (depth = one get_bids(%d) plus one get_asks(%d))\n\n", kDepth, kDepth);

    print_final(results.front().final_bbo, summary.checksum);
    return summary;
}

void print_comparison(const std::vector<Summary>& summaries) {
    std::printf("\n==========================================================================\n");
    std::printf("Summary, median of the runs above\n\n");
    std::printf("  %-22s", "");
    for (const Summary& s : summaries) std::printf(" %12c", s.key);
    std::printf("\n");

    auto row = [&summaries](const char* label, const char* format, auto field) {
        std::printf("  %-22s", label);
        for (const Summary& s : summaries) std::printf(format, field(s));
        std::printf("\n");
    };
    row("total ms", " %12.2f", [](const Summary& s) { return s.total_ms; });
    row("ns / add_order", " %12.1f", [](const Summary& s) { return s.add.mean; });
    row("ns / cancel_order", " %12.1f", [](const Summary& s) { return s.cancel.mean; });
    row("ns / get_bbo", " %12.1f", [](const Summary& s) { return s.bbo.mean; });
    row("ns / get_bbo (loop)", " %12.2f", [](const Summary& s) { return s.bbo_loop_ns; });
    row("ns / depth query", " %12.1f", [](const Summary& s) { return s.depth.mean; });
    row("p99 ns add_order", " %12.0f", [](const Summary& s) { return s.add.p99; });
    row("p99 ns cancel_order", " %12.0f", [](const Summary& s) { return s.cancel.p99; });
    row("p99.9 ns add_order", " %12.0f", [](const Summary& s) { return s.add.p999; });
    row("p99.9 ns cancel_order", " %12.0f", [](const Summary& s) { return s.cancel.p999; });
    if (summaries.front().ipc > 0) row("IPC", " %12.2f", [](const Summary& s) { return s.ipc; });
    row("page faults (pass 1)", " %12.0f", [](const Summary& s) { return s.page_faults; });

    bool same = true;
    for (const Summary& s : summaries) same = same && s.checksum == summaries.front().checksum;
    std::printf("\n  Checksums %s\n", same ? "match: every implementation returned the same results"
                                         : "DIFFER: the implementations returned different results");
}

template <class Book>
void perf_pass(const Impl& impl, const Workload& workload) {
    auto book = std::make_unique<Book>(workload.max_live_orders);
    const auto start = Clock::now();
    const std::uint64_t checksum = run_plain(*book, workload);
    const auto end = Clock::now();
    std::printf("Implementation %c (%s), one pass for perf\n", impl.key, impl.name);
    std::printf("Total time: %.2f ms\n\n", std::chrono::duration<double, std::milli>(end - start).count());
    print_final(book->get_bbo(), checksum);
    std::printf("Peak memory (RSS) of this process: %.1f MiB\n", peak_rss_mib());
}

// ---------------------------------------------------------------------------
// Command line

struct Options {
    std::string impl;  // "A", "B", "C", "all" or "none"
    bool verify = false;
    bool perf = false;
    int runs = 5;
    WorkloadConfig workload;
};

void print_usage(const char* program) {
    std::fprintf(stderr,
                 "usage: %s --verify\n"
                 "       %s --impl A|B|C|all [--runs N]\n"
                 "       %s --impl A|B|C|none --perf\n"
                 "options for all modes: --ops N (default 1000000), --seed N (default 42)\n",
                 program, program, program);
}

bool parse_number(const char* text, unsigned long long& value) {
    char* end = nullptr;
    value = std::strtoull(text, &end, 10);
    return end != text && *end == '\0';
}

bool parse_options(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = i + 1 < argc;
        unsigned long long number = 0;
        if (arg == "--verify") {
            options.verify = true;
        } else if (arg == "--perf") {
            options.perf = true;
        } else if (arg == "--impl" && has_value) {
            options.impl = argv[++i];
            if (options.impl.size() == 1) {
                options.impl[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(options.impl[0])));
            }
        } else if (arg == "--runs" && has_value && parse_number(argv[++i], number) && number >= 1 && number <= 1000) {
            options.runs = static_cast<int>(number);
        } else if (arg == "--ops" && has_value && parse_number(argv[++i], number) && number >= 1 &&
                   number <= 100000000) {
            options.workload.num_ops = static_cast<std::size_t>(number);
        } else if (arg == "--seed" && has_value && parse_number(argv[++i], number) && number <= 0xffffffffull) {
            options.workload.seed = static_cast<std::uint32_t>(number);
        } else {
            std::fprintf(stderr, "bad or incomplete option: %s\n", arg.c_str());
            return false;
        }
    }
    if (options.verify) return true;
    const std::string& impl = options.impl;
    if (options.perf) return impl == "A" || impl == "B" || impl == "C" || impl == "none";
    return impl == "A" || impl == "B" || impl == "C" || impl == "all";
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        print_usage(argv[0]);
        return 2;
    }
    if (options.verify) return run_verify(options.workload) ? 0 : 1;

    const Workload workload = generate_workload(options.workload);

    if (options.perf) {
        if (options.impl == "none") {
            // Baseline for perf: the same process minus the order book.
            std::uint64_t sum = 0;
            for (const Op& op : workload.ops) sum += op.id + op.qty;
            std::printf("Workload only, no order book: %zu operations generated (sum %llu)\n", workload.ops.size(),
                        static_cast<unsigned long long>(sum));
            std::printf("Peak memory (RSS) of this process: %.1f MiB\n", peak_rss_mib());
        } else if (options.impl == "A") {
            perf_pass<MapOrderBook>(kImplA, workload);
        } else if (options.impl == "B") {
            perf_pass<SortedVectorOrderBook>(kImplB, workload);
        } else {
            perf_pass<ArrayOrderBook>(kImplC, workload);
        }
        return 0;
    }

    const double timer_ns = timer_overhead_ns();
    std::printf("Order book benchmark\n");
    std::printf("  workload: %zu operations, seed %u\n", workload.ops.size(), options.workload.seed);
    std::printf("            %zu add_order, %zu cancel_order, %zu get_bbo\n", workload.adds, workload.cancels,
                workload.bbo_queries);
    std::printf("            plus get_bids(%d) and get_asks(%d) after every %d operations (%zu times)\n", kDepth,
                kDepth, kDepthEvery, workload.ops.size() / kDepthEvery);
    std::printf("            at most %zu orders resting at the same time\n", workload.max_live_orders);
    std::printf("  runs:     %d per implementation, median reported\n", options.runs);
    std::printf("  method:   pass 1 runs the workload with one clock read at each end (total ms,\n"
                "            IPC, page faults); pass 2 runs it again on a fresh book and times\n"
                "            every call with steady_clock (ns per call). get_bbo is quicker than\n"
                "            a clock read, so 'bbo loop' also times %d calls in a row on the\n"
                "            book left by pass 1\n",
                kBboLoopCalls);
    std::printf("  timer:    %.1f ns per pair of steady_clock::now() calls, subtracted from\n"
                "            the per-call times\n",
                timer_ns);

    std::vector<Contender> contenders;
    const bool all = options.impl == "all";
    if (all || options.impl == "A") contenders.push_back({kImplA, &run_once<MapOrderBook>, {}});
    if (all || options.impl == "B") contenders.push_back({kImplB, &run_once<SortedVectorOrderBook>, {}});
    if (all || options.impl == "C") contenders.push_back({kImplC, &run_once<ArrayOrderBook>, {}});
    if (all) {
        std::printf("            with --impl all the runs are interleaved (A B C, A B C, ...) so that\n"
                    "            background load and CPU heat affect every implementation equally\n");
    }

    Samples samples;
    samples.reserve(workload);
    for (int run = 1; run <= options.runs; ++run) {
        for (Contender& contender : contenders) {
            contender.results.push_back(contender.run_once(workload, samples, timer_ns));
            if (contender.results.back().checksum != contender.results.front().checksum) {
                std::fprintf(stderr, "error: run %d returned different results from run 1\n", run);
                return 1;
            }
        }
    }

    std::vector<Summary> summaries;
    for (const Contender& contender : contenders) summaries.push_back(report(contender.impl, contender.results));

    if (summaries.size() > 1) {
        print_comparison(summaries);
    } else {
        std::printf("Peak memory (RSS) of this process: %.1f MiB\n", peak_rss_mib());
    }
    return 0;
}
