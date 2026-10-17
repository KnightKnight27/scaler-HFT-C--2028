// Benchmark and correctness program for the three order books.
//
//   ./bench --impl A|B|C|all [--repeat 5] [--ops N] [--seed S] [--timing both|total|perop]
//   ./bench --verify         [--ops N] [--seed S]
//
// The implementation is picked once through a template parameter, so the
// hot loop has no virtual calls in it.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "array_book.h"
#include "heap_book.h"
#include "map_book.h"
#include "reference_book.h"
#include "workload.h"

namespace {

using Clock = std::chrono::steady_clock;

enum class Timing { Both, Total, PerOp };

struct Config {
    std::string impl;
    bool verify = false;
    int repeat = 1;
    Timing timing = Timing::Both;
    bool csv = false;
    WorkloadConfig wl;
};

// An empty book. "--impl none" runs the harness against it so the cost of
// everything that is not the order book can be measured and subtracted.
class NullBook {
public:
    static const char* name() { return "baseline, no order book (harness cost only)"; }
    void add_order(uint64_t, Side, double, long) {}
    bool cancel_order(uint64_t) { return false; }
    BBO get_bbo() const { return BBO{}; }
    std::vector<Level> get_bids(int = 10) const { return {}; }
    std::vector<Level> get_asks(int = 10) const { return {}; }
    size_t order_count() const { return 0; }
};

struct OpStats {
    uint64_t count = 0;
    uint64_t total_ns = 0;
    std::vector<uint32_t> samples;  // one per call, only in per-op mode
    double avg_ns() const { return count ? (double)total_ns / count : 0.0; }
};

struct RunResult {
    double total_ms = 0;
    OpStats add, cancel, bbo, depth;
    uint64_t checksum = 0;
    BBO final_bbo{};
    size_t live_orders = 0;
};

uint64_t ns_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
}

// Every result the book returns is folded into this checksum so the compiler
// cannot drop the work. It also has to come out identical for A, B and C.
void fold(uint64_t& cs, uint64_t v) { cs = (cs ^ v) * 0x100000001b3ull; }

void fold_bbo(uint64_t& cs, const BBO& b) {
    fold(cs, b.has_bid ? (uint64_t)to_tick(b.bid_price) : 0);
    fold(cs, b.has_bid ? (uint64_t)b.bid_qty : 0);
    fold(cs, b.has_ask ? (uint64_t)to_tick(b.ask_price) : 0);
    fold(cs, b.has_ask ? (uint64_t)b.ask_qty : 0);
}

void fold_levels(uint64_t& cs, const std::vector<Level>& v) {
    fold(cs, v.size());
    for (size_t i = 0; i < v.size(); i++) {
        fold(cs, (uint64_t)to_tick(v[i].price));
        fold(cs, (uint64_t)v[i].qty);
    }
}

void record(OpStats& s, Clock::time_point t0) {
    uint64_t ns = ns_between(t0, Clock::now());
    s.total_ns += ns;
    s.samples.push_back((uint32_t)ns);
}

// kPerOp is a compile time flag: true wraps every call in a timer, false
// times only the whole loop.
template <class Book, bool kPerOp>
RunResult run_workload(const std::vector<Op>& ops) {
    Book book;
    RunResult r;
    uint64_t cs = 0;
    Clock::time_point t0;

    if (kPerOp) {
        r.add.samples.reserve(ops.size());
        r.cancel.samples.reserve(ops.size() / 4);
        r.bbo.samples.reserve(ops.size() / 8);
        r.depth.samples.reserve(ops.size() / 100 + 1);
    }

    Clock::time_point t_start = Clock::now();
    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        if (op.type == OpType::Add) {
            if (kPerOp) t0 = Clock::now();
            book.add_order(op.id, op.side, to_price(op.tick), op.qty);
            if (kPerOp) record(r.add, t0);
            r.add.count++;
        } else if (op.type == OpType::Cancel) {
            if (kPerOp) t0 = Clock::now();
            bool ok = book.cancel_order(op.id);
            if (kPerOp) record(r.cancel, t0);
            r.cancel.count++;
            fold(cs, ok);
        } else {
            if (kPerOp) t0 = Clock::now();
            BBO b = book.get_bbo();
            if (kPerOp) record(r.bbo, t0);
            r.bbo.count++;
            fold_bbo(cs, b);
        }
        if ((i + 1) % 100 == 0) {
            if (kPerOp) t0 = Clock::now();
            std::vector<Level> bids = book.get_bids(10);
            std::vector<Level> asks = book.get_asks(10);
            if (kPerOp) record(r.depth, t0);
            r.depth.count++;
            fold_levels(cs, bids);
            fold_levels(cs, asks);
        }
    }
    Clock::time_point t_end = Clock::now();

    r.total_ms = ns_between(t_start, t_end) / 1e6;
    r.checksum = cs;
    r.final_bbo = book.get_bbo();
    r.live_orders = book.order_count();
    return r;
}

struct Percentiles {
    uint32_t p50 = 0, p90 = 0, p99 = 0, p999 = 0, max = 0;
};

Percentiles percentiles(std::vector<uint32_t>& v) {
    Percentiles p;
    if (v.empty()) return p;
    std::sort(v.begin(), v.end());
    size_t n = v.size() - 1;
    p.p50 = v[n * 50 / 100];
    p.p90 = v[n * 90 / 100];
    p.p99 = v[n * 99 / 100];
    p.p999 = v[n * 999 / 1000];
    p.max = v[n];
    return p;
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if (n % 2) return v[n / 2];
    return 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// Cost of the two clock reads that wrap every call in per-op mode.
double measure_timer_overhead() {
    const int n = 200000;
    Clock::time_point t0 = Clock::now();
    for (int i = 0; i < n; i++) {
        Clock::time_point a = Clock::now();
        Clock::time_point b = Clock::now();
        if (b < a) std::abort();
    }
    Clock::time_point t1 = Clock::now();
    return (double)ns_between(t0, t1) / n;
}

void print_bbo(const BBO& b) {
    std::printf("Final BBO:\n");
    if (b.has_bid) std::printf("  Bid: %.2f x %ld\n", b.bid_price, b.bid_qty);
    else           std::printf("  Bid: (none)\n");
    if (b.has_ask) std::printf("  Ask: %.2f x %ld\n", b.ask_price, b.ask_qty);
    else           std::printf("  Ask: (none)\n");
}

template <class Book>
int run_bench(const Config& cfg, const char* letter, const std::vector<Op>& ops, const WorkloadStats& ws) {
    std::printf("=== Implementation %s: %s ===\n", letter, Book::name());
    std::printf("workload: %zu ops, seed %u  (adds %zu, cancels %zu, bbo queries %zu, depth(10) queries %zu), live orders at end %zu\n",
                ops.size(), cfg.wl.seed, ws.adds, ws.cancels, ws.bbos, ops.size() / 100, ws.live_at_end);

    bool do_total = cfg.timing != Timing::PerOp;
    bool do_perop = cfg.timing != Timing::Total;
    if (do_perop) {
        std::printf("timer overhead: %.1f ns per steady_clock pair (included in the per-op numbers below)\n",
                    measure_timer_overhead());
    }

    std::vector<RunResult> totals, perops;
    std::vector<double> m_total, m_add, m_cancel, m_bbo, m_depth;

    std::printf("\n  run   total(ms)    ns/add  ns/cancel    ns/bbo  ns/depth10\n");
    for (int r = 0; r < cfg.repeat; r++) {
        double total_ms = 0;
        if (do_total) {
            totals.push_back(run_workload<Book, false>(ops));
            total_ms = totals.back().total_ms;
        }
        if (do_perop) {
            perops.push_back(run_workload<Book, true>(ops));
            if (!do_total) total_ms = perops.back().total_ms;
        }
        m_total.push_back(total_ms);
        if (do_perop) {
            const RunResult& p = perops.back();
            m_add.push_back(p.add.avg_ns());
            m_cancel.push_back(p.cancel.avg_ns());
            m_bbo.push_back(p.bbo.avg_ns());
            m_depth.push_back(p.depth.avg_ns());
            std::printf("  %3d  %10.2f  %8.1f  %9.1f  %8.1f  %10.1f\n", r + 1, total_ms,
                        p.add.avg_ns(), p.cancel.avg_ns(), p.bbo.avg_ns(), p.depth.avg_ns());
        } else {
            std::printf("  %3d  %10.2f         -          -         -           -\n", r + 1, total_ms);
        }
    }

    double med_total = median(m_total);
    if (do_perop) {
        std::printf("median %10.2f  %8.1f  %9.1f  %8.1f  %10.1f\n", med_total,
                    median(m_add), median(m_cancel), median(m_bbo), median(m_depth));
    } else {
        std::printf("median %10.2f   (%.1f M ops/s, loop timed as a whole, no per-op timers)\n",
                    med_total, ops.size() / med_total / 1e3);
    }

    // Percentiles come from the per-op run whose ns/add is closest to the median.
    Percentiles pa, pc, pb, pd;
    if (do_perop) {
        double med_add = median(m_add);
        size_t pick = 0;
        for (size_t i = 1; i < m_add.size(); i++)
            if (std::fabs(m_add[i] - med_add) < std::fabs(m_add[pick] - med_add)) pick = i;
        RunResult& p = perops[pick];
        pa = percentiles(p.add.samples);
        pc = percentiles(p.cancel.samples);
        pb = percentiles(p.bbo.samples);
        pd = percentiles(p.depth.samples);
        std::printf("\nlatency percentiles in ns (run %zu):\n", pick + 1);
        std::printf("               p50      p90      p99    p99.9      max\n");
        std::printf("  add      %7u  %7u  %7u  %7u  %7u\n", pa.p50, pa.p90, pa.p99, pa.p999, pa.max);
        std::printf("  cancel   %7u  %7u  %7u  %7u  %7u\n", pc.p50, pc.p90, pc.p99, pc.p999, pc.max);
        std::printf("  bbo      %7u  %7u  %7u  %7u  %7u\n", pb.p50, pb.p90, pb.p99, pb.p999, pb.max);
        std::printf("  depth10  %7u  %7u  %7u  %7u  %7u\n", pd.p50, pd.p90, pd.p99, pd.p999, pd.max);
    }

    // Final state is the same in every run; sanity check that and print it once.
    const RunResult& last = do_total ? totals.front() : perops.front();
    for (size_t i = 0; i < totals.size(); i++)
        if (totals[i].checksum != last.checksum) std::printf("WARNING: checksum differs between runs\n");
    for (size_t i = 0; i < perops.size(); i++)
        if (perops[i].checksum != last.checksum) std::printf("WARNING: checksum differs between runs\n");

    std::printf("\n");
    print_bbo(last.final_bbo);
    std::printf("Checksum: %llu\n\n", (unsigned long long)last.checksum);

    if (cfg.csv) {
        std::printf("CSV,%s,%zu,%u,%.3f,%.1f,%.1f,%.1f,%.1f,%u,%u,%u,%u,%u,%u,%u,%u,%u,%llu\n", letter, ops.size(),
                    cfg.wl.seed, med_total, median(m_add), median(m_cancel), median(m_bbo), median(m_depth),
                    pa.p50, pa.p99, pa.p999, pc.p50, pc.p99, pc.p999, pb.p50, pb.p99, pb.p999,
                    (unsigned long long)last.checksum);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// --verify: same workload through A, B and C side by side.
// ---------------------------------------------------------------------------

template <class Book>
bool edge_cases_ok(std::string& why) {
    Book book;
    BBO b = book.get_bbo();
    if (b.has_bid || b.has_ask) { why = "empty book reported a bid or ask"; return false; }
    if (book.cancel_order(12345)) { why = "cancel of unknown id returned true"; return false; }
    book.add_order(1, Side::Buy, 99.00, 5);
    book.add_order(2, Side::Buy, 98.00, 5);
    book.add_order(3, Side::Buy, 97.00, 5);
    if (book.get_bids(10).size() != 3) { why = "get_bids(10) on a 3-level book did not return 3 levels"; return false; }
    if (!book.cancel_order(2)) { why = "cancel of a live order returned false"; return false; }
    if (book.cancel_order(2)) { why = "second cancel of the same id returned true"; return false; }
    if (book.get_bids(10).size() != 2) { why = "level did not disappear after cancel"; return false; }
    return true;
}

struct VerifyState {
    bool fail[3] = {false, false, false};
    std::string why[3];

    void flag(int k, const std::string& msg) {
        if (!fail[k]) { fail[k] = true; why[k] = msg; }
    }

    // ab/ac/bc say which pairs agree. Whoever is in the minority is wrong.
    void vote(bool ab, bool ac, bool bc, const char* what, size_t op_index) {
        if (ab && ac) return;
        std::string msg = std::string(what) + " differs at op " + std::to_string(op_index);
        if (ab)      flag(2, msg);
        else if (ac) flag(1, msg);
        else if (bc) flag(0, msg);
        else { flag(0, msg); flag(1, msg); flag(2, msg); }
    }
};

int run_verify(const Config& cfg) {
    std::printf("Running correctness test...\n\n");

    std::printf("Edge cases (empty book, unknown cancel, double cancel, insufficient depth):\n");
    std::string why;
    bool edge_ok[3];
    edge_ok[0] = edge_cases_ok<MapBook>(why);
    std::printf("  Implementation A: %s%s\n", edge_ok[0] ? "ok" : "FAIL - ", edge_ok[0] ? "" : why.c_str());
    edge_ok[1] = edge_cases_ok<HeapBook>(why);
    std::printf("  Implementation B: %s%s\n", edge_ok[1] ? "ok" : "FAIL - ", edge_ok[1] ? "" : why.c_str());
    edge_ok[2] = edge_cases_ok<ArrayBook>(why);
    std::printf("  Implementation C: %s%s\n", edge_ok[2] ? "ok" : "FAIL - ", edge_ok[2] ? "" : why.c_str());

    WorkloadStats ws;
    std::vector<Op> ops = generate_workload(cfg.wl, &ws);
    const size_t ref_every = 10000;
    std::printf("\nRandom workload: %zu ops, seed %u (adds %zu, cancels %zu, bbo %zu)\n", ops.size(), cfg.wl.seed,
                ws.adds, ws.cancels, ws.bbos);
    std::printf("  BBO compared after every op, depth(10) every 100 ops,\n"
                "  full book compared against a brute-force reference every %zu ops and at the end.\n\n", ref_every);

    MapBook a;
    HeapBook b;
    ArrayBook c;
    ReferenceBook ref;
    VerifyState vs;
    uint64_t cs[3] = {0, 0, 0};

    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        if (op.type == OpType::Add) {
            double price = to_price(op.tick);
            a.add_order(op.id, op.side, price, op.qty);
            b.add_order(op.id, op.side, price, op.qty);
            c.add_order(op.id, op.side, price, op.qty);
            ref.add_order(op.id, op.side, price, op.qty);
        } else if (op.type == OpType::Cancel) {
            bool ra = a.cancel_order(op.id);
            bool rb = b.cancel_order(op.id);
            bool rc = c.cancel_order(op.id);
            bool rr = ref.cancel_order(op.id);
            vs.vote(ra == rb, ra == rc, rb == rc, "cancel_order result", i);
            if (ra != rr) vs.flag(0, "cancel result differs from reference at op " + std::to_string(i));
            if (rb != rr) vs.flag(1, "cancel result differs from reference at op " + std::to_string(i));
            if (rc != rr) vs.flag(2, "cancel result differs from reference at op " + std::to_string(i));
            fold(cs[0], ra);
            fold(cs[1], rb);
            fold(cs[2], rc);
        }

        BBO ba = a.get_bbo(), bb = b.get_bbo(), bc = c.get_bbo();
        vs.vote(same_bbo(ba, bb), same_bbo(ba, bc), same_bbo(bb, bc), "get_bbo", i);
        if (op.type == OpType::Bbo) {
            fold_bbo(cs[0], ba);
            fold_bbo(cs[1], bb);
            fold_bbo(cs[2], bc);
        }

        if ((i + 1) % 100 == 0) {
            std::vector<Level> la = a.get_bids(10), lb = b.get_bids(10), lc = c.get_bids(10);
            vs.vote(same_levels(la, lb), same_levels(la, lc), same_levels(lb, lc), "get_bids(10)", i);
            std::vector<Level> ka = a.get_asks(10), kb = b.get_asks(10), kc = c.get_asks(10);
            vs.vote(same_levels(ka, kb), same_levels(ka, kc), same_levels(kb, kc), "get_asks(10)", i);
            fold_levels(cs[0], la); fold_levels(cs[0], ka);
            fold_levels(cs[1], lb); fold_levels(cs[1], kb);
            fold_levels(cs[2], lc); fold_levels(cs[2], kc);
        }

        if ((i + 1) % ref_every == 0 || i + 1 == ops.size()) {
            BBO br = ref.get_bbo();
            std::vector<Level> rb = ref.get_bids(kLevelsPerSide), ra = ref.get_asks(kLevelsPerSide);
            std::string at = " vs reference at op " + std::to_string(i);
            if (!same_bbo(ba, br)) vs.flag(0, "get_bbo" + at);
            if (!same_bbo(bb, br)) vs.flag(1, "get_bbo" + at);
            if (!same_bbo(bc, br)) vs.flag(2, "get_bbo" + at);
            if (!same_levels(a.get_bids(kLevelsPerSide), rb) || !same_levels(a.get_asks(kLevelsPerSide), ra)) vs.flag(0, "full depth" + at);
            if (!same_levels(b.get_bids(kLevelsPerSide), rb) || !same_levels(b.get_asks(kLevelsPerSide), ra)) vs.flag(1, "full depth" + at);
            if (!same_levels(c.get_bids(kLevelsPerSide), rb) || !same_levels(c.get_asks(kLevelsPerSide), ra)) vs.flag(2, "full depth" + at);
            if (a.order_count() != ref.order_count()) vs.flag(0, "order count" + at);
            if (b.order_count() != ref.order_count()) vs.flag(1, "order count" + at);
            if (c.order_count() != ref.order_count()) vs.flag(2, "order count" + at);
        }
    }

    const char* names[3] = {"A", "B", "C"};
    bool all_ok = true;
    for (int k = 0; k < 3; k++) {
        bool ok = edge_ok[k] && !vs.fail[k];
        all_ok = all_ok && ok;
        if (ok) std::printf("Implementation %s: PASS\n", names[k]);
        else    std::printf("Implementation %s: FAIL (%s)\n", names[k], vs.fail[k] ? vs.why[k].c_str() : "edge cases");
    }
    std::printf("\n");
    if (all_ok && cs[0] == cs[1] && cs[1] == cs[2]) {
        std::printf("All implementations produce identical results.\n");
        std::printf("Checksum (A = B = C): %llu\n", (unsigned long long)cs[0]);
        print_bbo(a.get_bbo());
        return 0;
    }
    std::printf("Implementations DISAGREE.\n");
    std::printf("Checksums: A=%llu B=%llu C=%llu\n", (unsigned long long)cs[0],
                (unsigned long long)cs[1], (unsigned long long)cs[2]);
    return 1;
}

void usage() {
    std::printf(
        "usage: bench --impl A|B|C|all|none [options]   (none = empty baseline, harness cost only)\n"
        "       bench --verify [options]\n"
        "options:\n"
        "  --repeat N        run the benchmark N times and report the median (default 1)\n"
        "  --ops N           number of operations (default 1000000)\n"
        "  --seed S          random seed (default 42)\n"
        "  --band N          use only the N best price levels per side (default 5000 = all)\n"
        "  --timing MODE     both | total | perop (default both)\n"
        "                    total: time the whole loop only (use this under perf)\n"
        "                    perop: wrap every call in steady_clock to get per-op averages/percentiles\n"
        "  --csv             also print one machine readable CSV line per implementation\n");
}

// Returns the value after an option, or nullptr if there is none.
const char* value_of(int& i, int argc, char** argv) {
    if (i + 1 >= argc) {
        std::fprintf(stderr, "%s needs a value\n", argv[i]);
        return nullptr;
    }
    i++;
    return argv[i];
}

}  // namespace

int main(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--verify") {
            cfg.verify = true;
        } else if (arg == "--csv") {
            cfg.csv = true;
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else if (arg == "--impl" || arg == "--repeat" || arg == "--ops" || arg == "--seed" ||
                   arg == "--band" || arg == "--timing") {
            const char* v = value_of(i, argc, argv);
            if (!v) return 2;
            if (arg == "--impl")        cfg.impl = v;
            else if (arg == "--repeat") cfg.repeat = std::atoi(v);
            else if (arg == "--ops")    cfg.wl.ops = std::atoll(v);
            else if (arg == "--seed")   cfg.wl.seed = std::atoll(v);
            else if (arg == "--band")   cfg.wl.band = std::atoi(v);
            else {
                std::string m = v;
                if (m == "both") cfg.timing = Timing::Both;
                else if (m == "total") cfg.timing = Timing::Total;
                else if (m == "perop") cfg.timing = Timing::PerOp;
                else { std::fprintf(stderr, "unknown timing mode %s\n", v); return 2; }
            }
        } else {
            std::fprintf(stderr, "unknown option %s\n", arg.c_str());
            usage();
            return 2;
        }
    }
    if (cfg.repeat < 1) cfg.repeat = 1;
    if (cfg.wl.band < 1 || cfg.wl.band > kLevelsPerSide) cfg.wl.band = kLevelsPerSide;

    if (cfg.verify) return run_verify(cfg);

    if (cfg.impl.empty()) { usage(); return 2; }
    for (size_t i = 0; i < cfg.impl.size(); i++) cfg.impl[i] = std::toupper(cfg.impl[i]);

    WorkloadStats ws;
    std::vector<Op> ops = generate_workload(cfg.wl, &ws);

    if (cfg.impl == "NONE") return run_bench<NullBook>(cfg, "none", ops, ws);
    if (cfg.impl != "A" && cfg.impl != "B" && cfg.impl != "C" && cfg.impl != "ALL") {
        std::fprintf(stderr, "unknown implementation '%s' (use A, B, C, all or none)\n", cfg.impl.c_str());
        return 2;
    }
    int rc = 0;
    if (cfg.impl == "A" || cfg.impl == "ALL") rc |= run_bench<MapBook>(cfg, "A", ops, ws);
    if (cfg.impl == "B" || cfg.impl == "ALL") rc |= run_bench<HeapBook>(cfg, "B", ops, ws);
    if (cfg.impl == "C" || cfg.impl == "ALL") rc |= run_bench<ArrayBook>(cfg, "C", ops, ws);
    return rc;
}
