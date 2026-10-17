// =============================================================================
// bench.cpp  –  Deterministic benchmark and --verify entry point
// =============================================================================
// Usage:
//   .\bench.exe --impl A          benchmark MapOrderBook
//   .\bench.exe --impl B          benchmark VectorOrderBook
//   .\bench.exe --impl C          benchmark ArrayOrderBook
//   .\bench.exe --verify          cross-implementation correctness verifier
//
// Workload
// --------
//   Total operations     : 1,000,000
//   ~70% add  ~20% cancel  ~10% BBO query
//   get_bids(10) + get_asks(10) every 100 operations
//   50% buy / 50% sell
//   qty 1..100, random valid tick per side
//   All cancellations target a currently-live order (generated from reference model)
//
// Measurement
// -----------
//   Workload trace is generated ONCE (excluding from timing).
//   Each impl is run 5 times; median is reported.
//   std::chrono::steady_clock used for timing.
//   Results are checksummed to prevent dead-code elimination.
// =============================================================================

#include "order_book.hpp"
#include "map_order_book.hpp"
#include "vector_order_book.hpp"
#include "array_order_book.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <random>
#include <string>
#include <cstring>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <cassert>
#include <memory>
#include <stdexcept>
#include <functional>

using namespace hft;
using Clock = std::chrono::steady_clock;
using NS    = std::chrono::nanoseconds;

// ---------------------------------------------------------------------------
// Operation trace types
// ---------------------------------------------------------------------------
enum class OpKind : uint8_t { ADD, CANCEL, BBO };

struct TraceOp {
    OpKind        kind;
    std::uint64_t id;       // ADD: new id; CANCEL: id to cancel
    Side          side;
    int           tick;     // for ADD
    long          qty;      // for ADD
};

// ---------------------------------------------------------------------------
// Generate deterministic workload trace (excluded from timing)
// ---------------------------------------------------------------------------
// Uses a reference "shadow" model to ensure every CANCEL targets a live order.
// The reference model tracks live IDs but does NOT implement an order book —
// it only tracks which IDs are currently active.
// ---------------------------------------------------------------------------
static std::vector<TraceOp> generate_trace(int n_ops, uint32_t seed = 42)
{
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> bid_tick_d(TICK_BID_MIN, TICK_BID_MAX);
    std::uniform_int_distribution<int> ask_tick_d(TICK_ASK_MIN, TICK_ASK_MAX);
    std::uniform_int_distribution<int> qty_d(1, 100);
    std::uniform_int_distribution<int> side_d(0, 1);
    // 70% add, 20% cancel, 10% BBO
    // We use a 10-sided die: 0-6 → ADD, 7-8 → CANCEL, 9 → BBO
    std::uniform_int_distribution<int> op_d(0, 9);

    std::vector<TraceOp> trace;
    trace.reserve(n_ops);

    std::vector<std::uint64_t> live_ids;
    live_ids.reserve(1000);
    std::uint64_t next_id = 1;

    for (int i = 0; i < n_ops; ++i) {
        int r = op_d(rng);
        if (r <= 6 || live_ids.empty()) {
            // ADD
            TraceOp op;
            op.kind = OpKind::ADD;
            op.id   = next_id++;
            op.side = side_d(rng) ? Side::Buy : Side::Sell;
            op.tick = (op.side == Side::Buy) ? bid_tick_d(rng) : ask_tick_d(rng);
            op.qty  = qty_d(rng);
            live_ids.push_back(op.id);
            trace.push_back(op);
        } else if (r <= 8 && !live_ids.empty()) {
            // CANCEL – select a live order
            std::uniform_int_distribution<std::size_t> idx_d(0, live_ids.size()-1);
            std::size_t idx = idx_d(rng);
            TraceOp op;
            op.kind = OpKind::CANCEL;
            op.id   = live_ids[idx];
            live_ids.erase(live_ids.begin() + static_cast<std::ptrdiff_t>(idx));
            trace.push_back(op);
        } else {
            TraceOp op;
            op.kind = OpKind::BBO;
            trace.push_back(op);
        }
    }
    return trace;
}

// ---------------------------------------------------------------------------
// Replay trace against an IOrderBook; measure and return timing per op kind
// ---------------------------------------------------------------------------
struct RunResult {
    long long total_ns;
    long long add_ns;
    long long cancel_ns;
    long long bbo_ns;
    long long add_count;
    long long cancel_count;
    long long bbo_count;
    long long depth_count;  // number of depth queries (every 100 ops)
    // checksum to prevent dead-code elimination
    long long checksum;
};

static RunResult replay(IOrderBook& book, const std::vector<TraceOp>& trace)
{
    RunResult res{};
    long long chk = 0;

    auto t_total_start = Clock::now();

    for (std::size_t i = 0; i < trace.size(); ++i) {
        const TraceOp& op = trace[i];

        if (op.kind == OpKind::ADD) {
            auto t0 = Clock::now();
            book.add_order(op.id, op.side, tick_to_price(op.tick), op.qty);
            auto t1 = Clock::now();
            res.add_ns += std::chrono::duration_cast<NS>(t1 - t0).count();
            ++res.add_count;
        } else if (op.kind == OpKind::CANCEL) {
            auto t0 = Clock::now();
            bool r = book.cancel_order(op.id);
            auto t1 = Clock::now();
            res.cancel_ns += std::chrono::duration_cast<NS>(t1 - t0).count();
            ++res.cancel_count;
            chk += r ? 1 : 0;
        } else { // BBO
            auto t0 = Clock::now();
            BBO b = book.get_bbo();
            auto t1 = Clock::now();
            res.bbo_ns += std::chrono::duration_cast<NS>(t1 - t0).count();
            ++res.bbo_count;
            // consume result
            chk += b.has_bid ? static_cast<long long>(b.bid_qty) : 0;
            chk += b.has_ask ? static_cast<long long>(b.ask_qty) : 0;
        }

        // Depth queries every 100 operations (not included in op-category timings above)
        if ((i + 1) % 100 == 0) {
            auto bids = book.get_bids(10);
            auto asks = book.get_asks(10);
            for (auto& l : bids) chk += l.qty;
            for (auto& l : asks) chk += l.qty;
            ++res.depth_count;
        }
    }

    auto t_total_end = Clock::now();
    res.total_ns = std::chrono::duration_cast<NS>(t_total_end - t_total_start).count();
    res.checksum  = chk;
    return res;
}

// ---------------------------------------------------------------------------
// Print a single run's results
// ---------------------------------------------------------------------------
static void print_result(const RunResult& r, int run_no)
{
    auto ns_per = [](long long ns, long long cnt) -> double {
        return cnt > 0 ? static_cast<double>(ns) / static_cast<double>(cnt) : 0.0;
    };
    std::cout << "  Run " << run_no << "  total=" << r.total_ns / 1'000'000 << " ms"
              << "  add=" << std::fixed << std::setprecision(1) << ns_per(r.add_ns, r.add_count) << " ns/op"
              << "  cancel=" << ns_per(r.cancel_ns, r.cancel_count) << " ns/op"
              << "  bbo=" << ns_per(r.bbo_ns, r.bbo_count) << " ns/op"
              << "  chk=" << r.checksum << "\n";
}

// ---------------------------------------------------------------------------
// Compute per-metric medians independently across N runs.
//
// Sorting all runs by total_ns and picking the middle row gives the correct
// median total time, but gives an ARBITRARY value for add_ns, cancel_ns, etc.
// because there is no reason the run with the median total is also the run
// with the median add latency.
//
// Instead we sort each metric's values independently and pick the middle one.
// For N=5 (odd), the median is the 3rd value (index 2) after sorting.
// Operation counts are taken from run 0 — they are identical across all runs
// because the trace is fixed.
// ---------------------------------------------------------------------------
struct MedianMetrics {
    long long total_ns;    // median total wall time
    double    add_ns;      // median avg ns/add
    double    cancel_ns;   // median avg ns/cancel
    double    bbo_ns;      // median avg ns/bbo
    long long add_count;
    long long cancel_count;
    long long bbo_count;
    long long depth_count;
    long long checksum;    // taken from run 0 (all runs must agree)
};

static MedianMetrics compute_medians(std::vector<RunResult>& runs)
{
    const std::size_t N = runs.size();
    const std::size_t mid = N / 2;  // for N=5, mid=2 (0-indexed 3rd value)

    auto median_ll = [&](std::function<long long(const RunResult&)> key) -> long long {
        std::vector<long long> vals;
        vals.reserve(N);
        for (auto& r : runs) vals.push_back(key(r));
        std::sort(vals.begin(), vals.end());
        return vals[mid];
    };

    auto ns_per = [](long long ns, long long cnt) -> double {
        return cnt > 0 ? static_cast<double>(ns) / static_cast<double>(cnt) : 0.0;
    };

    // Compute per-run avg latencies, then take the median of those averages.
    auto median_avg = [&](std::function<long long(const RunResult&)> ns_key,
                          std::function<long long(const RunResult&)> cnt_key) -> double {
        std::vector<double> vals;
        vals.reserve(N);
        for (auto& r : runs) vals.push_back(ns_per(ns_key(r), cnt_key(r)));
        std::sort(vals.begin(), vals.end());
        return vals[mid];
    };

    MedianMetrics m;
    m.total_ns     = median_ll([](const RunResult& r){ return r.total_ns; });
    m.add_ns       = median_avg(
        [](const RunResult& r){ return r.add_ns; },
        [](const RunResult& r){ return r.add_count; });
    m.cancel_ns    = median_avg(
        [](const RunResult& r){ return r.cancel_ns; },
        [](const RunResult& r){ return r.cancel_count; });
    m.bbo_ns       = median_avg(
        [](const RunResult& r){ return r.bbo_ns; },
        [](const RunResult& r){ return r.bbo_count; });
    // Counts and checksum are invariant across runs (same trace, same book).
    m.add_count    = runs[0].add_count;
    m.cancel_count = runs[0].cancel_count;
    m.bbo_count    = runs[0].bbo_count;
    m.depth_count  = runs[0].depth_count;
    m.checksum     = runs[0].checksum;
    return m;
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
static std::unique_ptr<IOrderBook> make_book(char impl)
{
    switch (impl) {
        case 'A': case 'a': return std::make_unique<MapOrderBook>();
        case 'B': case 'b': return std::make_unique<VectorOrderBook>();
        case 'C': case 'c': return std::make_unique<ArrayOrderBook>();
        default: throw std::invalid_argument(std::string("Unknown impl: ") + impl);
    }
}

// ---------------------------------------------------------------------------
// Verify mode: compare all three implementations on every op
// ---------------------------------------------------------------------------
static int run_verify(const std::vector<TraceOp>& trace)
{
    std::cout << "=== VERIFY MODE ===\n";
    std::cout << "Trace size: " << trace.size() << " ops\n";

    MapOrderBook    ma;
    VectorOrderBook vb;
    ArrayOrderBook  ac;

    int mismatches = 0;

    auto feq = [](double a, double b) { return std::fabs(a - b) < 0.001; };
    auto bbo_eq = [&](const BBO& x, const BBO& y) {
        if (x.has_bid != y.has_bid || x.has_ask != y.has_ask) return false;
        if (x.has_bid && (!feq(x.bid_price, y.bid_price) || x.bid_qty != y.bid_qty)) return false;
        if (x.has_ask && (!feq(x.ask_price, y.ask_price) || x.ask_qty != y.ask_qty)) return false;
        return true;
    };
    auto levels_eq = [&](const std::vector<Level>& a, const std::vector<Level>& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!feq(a[i].price, b[i].price) || a[i].qty != b[i].qty) return false;
        }
        return true;
    };

    for (std::size_t i = 0; i < trace.size(); ++i) {
        const TraceOp& op = trace[i];

        // Apply op
        if (op.kind == OpKind::ADD) {
            bool ok_a = true, ok_b = true, ok_c = true;
            try { ma.add_order(op.id, op.side, tick_to_price(op.tick), op.qty); } catch (...) { ok_a = false; }
            try { vb.add_order(op.id, op.side, tick_to_price(op.tick), op.qty); } catch (...) { ok_b = false; }
            try { ac.add_order(op.id, op.side, tick_to_price(op.tick), op.qty); } catch (...) { ok_c = false; }
            if (ok_a != ok_b || ok_b != ok_c) {
                std::cerr << "MISMATCH ADD op=" << i << " id=" << op.id
                          << " A=" << ok_a << " B=" << ok_b << " C=" << ok_c << "\n";
                ++mismatches;
            }
        } else if (op.kind == OpKind::CANCEL) {
            bool ra = ma.cancel_order(op.id);
            bool rb = vb.cancel_order(op.id);
            bool rc = ac.cancel_order(op.id);
            if (ra != rb || rb != rc) {
                std::cerr << "MISMATCH CANCEL op=" << i << " id=" << op.id
                          << " A=" << ra << " B=" << rb << " C=" << rc << "\n";
                ++mismatches;
            }
        } else {
            BBO ba = ma.get_bbo(), bb = vb.get_bbo(), bc = ac.get_bbo();
            if (!bbo_eq(ba, bb) || !bbo_eq(bb, bc)) {
                std::cerr << "MISMATCH BBO op=" << i << "\n";
                ++mismatches;
            }
        }

        // After every op: BBO + depth check
        BBO ba = ma.get_bbo(), bb = vb.get_bbo(), bc = ac.get_bbo();
        if (!bbo_eq(ba, bb) || !bbo_eq(bb, bc)) {
            std::cerr << "POST-OP BBO MISMATCH at op=" << i << "\n";
            std::cerr << "  A: hb=" << ba.has_bid << " bp=" << ba.bid_price << " bq=" << ba.bid_qty
                      << " ha=" << ba.has_ask << " ap=" << ba.ask_price << " aq=" << ba.ask_qty << "\n";
            std::cerr << "  B: hb=" << bb.has_bid << " bp=" << bb.bid_price << " bq=" << bb.bid_qty
                      << " ha=" << bb.has_ask << " ap=" << bb.ask_price << " aq=" << bb.ask_qty << "\n";
            std::cerr << "  C: hb=" << bc.has_bid << " bp=" << bc.bid_price << " bq=" << bc.bid_qty
                      << " ha=" << bc.has_ask << " ap=" << bc.ask_price << " aq=" << bc.ask_qty << "\n";
            ++mismatches;
        }

        auto bids_a = ma.get_bids(10), bids_b = vb.get_bids(10), bids_c = ac.get_bids(10);
        if (!levels_eq(bids_a, bids_b) || !levels_eq(bids_b, bids_c)) {
            std::cerr << "BID DEPTH MISMATCH at op=" << i << "\n";
            ++mismatches;
        }
        auto asks_a = ma.get_asks(10), asks_b = vb.get_asks(10), asks_c = ac.get_asks(10);
        if (!levels_eq(asks_a, asks_b) || !levels_eq(asks_b, asks_c)) {
            std::cerr << "ASK DEPTH MISMATCH at op=" << i << "\n";
            ++mismatches;
        }

        if (mismatches > 20) {
            std::cerr << "Too many mismatches – aborting verify early\n";
            break;
        }
    }

    // Final BBO
    BBO fa = ma.get_bbo();
    std::cout << "\nFinal BBO (Impl A/reference):\n";
    if (fa.has_bid)
        std::cout << "  Bid: " << std::fixed << std::setprecision(2)
                  << fa.bid_price << " x " << fa.bid_qty << "\n";
    else
        std::cout << "  Bid: (none)\n";
    if (fa.has_ask)
        std::cout << "  Ask: " << std::fixed << std::setprecision(2)
                  << fa.ask_price << " x " << fa.ask_qty << "\n";
    else
        std::cout << "  Ask: (none)\n";


    if (mismatches == 0)
        std::cout << "\nOverall verify: PASS\n";
    else
        std::cout << "\nOverall verify: FAIL (" << mismatches << " mismatches)\n";

    return mismatches;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    constexpr int N_OPS  = 1'000'000;
    constexpr int N_RUNS = 5;

    // Parse arguments
    bool do_verify = false;
    char impl_char = 0;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--verify") == 0) {
            do_verify = true;
        } else if (std::strcmp(argv[i], "--impl") == 0 && i + 1 < argc) {
            impl_char = argv[++i][0];
        }
    }

    if (!do_verify && impl_char == 0) {
        std::cerr << "Usage: bench --impl {A|B|C} | --verify\n";
        return 1;
    }

    // Generate trace once (excluded from timing)
    std::cout << "Generating " << N_OPS << " op trace (seed=42)...\n";
    auto trace = generate_trace(N_OPS, 42);

    // Count op distribution for display
    {
        long na = 0, nc = 0, nb = 0;
        for (auto& t : trace) {
            if (t.kind == OpKind::ADD) ++na;
            else if (t.kind == OpKind::CANCEL) ++nc;
            else ++nb;
        }
        std::cout << "Trace: ADD=" << na << " CANCEL=" << nc << " BBO=" << nb << "\n";
        std::cout << "Depth queries (every 100 ops): " << trace.size() / 100 << "\n\n";
    }

    if (do_verify) {
        return run_verify(trace) == 0 ? 0 : 1;
    }

    // Benchmark
    const std::string impl_name = [impl_char]() -> std::string {
        switch (impl_char) {
            case 'A': case 'a': return "A (MapOrderBook – std::map)";
            case 'B': case 'b': return "B (VectorOrderBook – sorted vector)";
            case 'C': case 'c': return "C (ArrayOrderBook – fixed array)";
            default: return "Unknown";
        }
    }();

    std::cout << "Benchmarking implementation: " << impl_name << "\n";
    std::cout << "Runs: " << N_RUNS << " (will report median)\n\n";

    std::vector<RunResult> runs;
    runs.reserve(N_RUNS);

    for (int run = 1; run <= N_RUNS; ++run) {
        auto book = make_book(impl_char);
        RunResult r = replay(*book, trace);
        print_result(r, run);
        runs.push_back(r);
    }

    MedianMetrics med = compute_medians(runs);

    std::cout << "\n--- Median Results (each metric sorted independently) ---\n";
    std::cout << "  Implementation  : " << impl_name << "\n";
    std::cout << "  Total time      : " << med.total_ns / 1'000'000 << " ms  (median of " << N_RUNS << " runs)\n";
    std::cout << "  ADD ops         : " << med.add_count
              << "  median avg " << std::fixed << std::setprecision(1)
              << med.add_ns << " ns/op\n";
    std::cout << "  CANCEL ops      : " << med.cancel_count
              << "  median avg " << med.cancel_ns << " ns/op\n";
    std::cout << "  BBO ops         : " << med.bbo_count
              << "  median avg " << med.bbo_ns << " ns/op\n";
    std::cout << "  Depth queries   : " << med.depth_count
              << " (cost included in total time; not timed separately)\n";
    std::cout << "  Checksum        : " << med.checksum
              << " (must match across all implementations)\n";

    // Final BBO (anti dead-code)
    {
        auto book = make_book(impl_char);
        for (auto& op : trace) {
            if (op.kind == OpKind::ADD)
                book->add_order(op.id, op.side, tick_to_price(op.tick), op.qty);
            else if (op.kind == OpKind::CANCEL)
                book->cancel_order(op.id);
        }
        BBO b = book->get_bbo();
        std::cout << "\nFinal BBO after full trace replay:\n";
        if (b.has_bid)
            std::cout << "  Bid: " << std::fixed << std::setprecision(2)
                      << b.bid_price << " x " << b.bid_qty << "\n";
        else
            std::cout << "  Bid: (none)\n";
        if (b.has_ask)
            std::cout << "  Ask: " << std::fixed << std::setprecision(2)
                      << b.ask_price << " x " << b.ask_qty << "\n";
        else
            std::cout << "  Ask: (none)\n";
    }

    return 0;
}
