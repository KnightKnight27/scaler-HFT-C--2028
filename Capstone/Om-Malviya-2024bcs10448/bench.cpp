// bench.cpp — one program for all three order books.
//
//   ./bench --verify          run A, B, C on the same workload and prove they agree
//   ./bench --impl A|B|C      benchmark one implementation (5 runs, median)
//   ./bench --impl A --ops N  override the op count (default 1,000,000)
//
// The workload is generated ONCE from a fixed seed (std::mt19937(42)) so it is
// identical and repeatable across every implementation and every run.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "common.h"
#include "impl_a_map.h"
#include "impl_b_hashmap.h"
#include "impl_c_array.h"

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// Workload
// ---------------------------------------------------------------------------
enum class OpType : uint8_t { Add, Cancel, Bbo };

struct Op {
    OpType type;
    Side side;
    double price;
    long qty;
    uint64_t id;
};

// Op mix: ~70% add, ~20% cancel, ~10% bbo. A cancel always targets a currently
// live order; if nothing is live yet it degrades to an add.
static std::vector<Op> generate_workload(size_t n) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> op_pick(0, 99);
    std::uniform_int_distribution<int> side_pick(0, 1);
    std::uniform_int_distribution<long> qty_pick(1, 100);
    std::uniform_int_distribution<int> bid_tick(ticks::kBidMin, ticks::kBidMax);
    std::uniform_int_distribution<int> ask_tick(ticks::kAskMin, ticks::kAskMax);

    std::vector<Op> ops;
    ops.reserve(n);
    std::vector<uint64_t> live;   // ids of orders currently resting
    live.reserve(n);
    uint64_t next_id = 1;

    for (size_t i = 0; i < n; ++i) {
        const int r = op_pick(rng);
        if (r < 70 || live.empty()) {
            const Side side = side_pick(rng) ? Side::Buy : Side::Sell;
            const int tick = (side == Side::Buy) ? bid_tick(rng) : ask_tick(rng);
            const long qty = qty_pick(rng);
            const uint64_t id = next_id++;
            ops.push_back({OpType::Add, side, ticks::to_price(tick), qty, id});
            live.push_back(id);
        } else if (r < 90) {
            std::uniform_int_distribution<size_t> idx(0, live.size() - 1);
            const size_t j = idx(rng);
            const uint64_t id = live[j];
            live[j] = live.back();
            live.pop_back();
            ops.push_back({OpType::Cancel, Side::Buy, 0.0, 0, id});
        } else {
            ops.push_back({OpType::Bbo, Side::Buy, 0.0, 0, 0});
        }
    }
    return ops;
}

// ---------------------------------------------------------------------------
// Timing a single implementation
// ---------------------------------------------------------------------------
struct RunMetrics {
    double total_ms;
    double ns_add;
    double ns_cancel;
    double ns_bbo;
    uint64_t checksum;
    BBO final_bbo;
};

template <typename OB>
static RunMetrics run_once(const std::vector<Op>& ops) {
    OB ob;
    long long ns_add = 0, ns_cancel = 0, ns_bbo = 0;
    long long n_add = 0, n_cancel = 0, n_bbo = 0;
    uint64_t checksum = 0;

    const auto t_start = Clock::now();
    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        switch (op.type) {
            case OpType::Add: {
                const auto a = Clock::now();
                ob.add_order(op.id, op.side, op.price, op.qty);
                const auto b = Clock::now();
                ns_add += std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
                ++n_add;
                break;
            }
            case OpType::Cancel: {
                const auto a = Clock::now();
                const bool ok = ob.cancel_order(op.id);
                const auto b = Clock::now();
                ns_cancel += std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
                ++n_cancel;
                checksum += ok ? 1u : 0u;
                break;
            }
            case OpType::Bbo: {
                const auto a = Clock::now();
                const BBO bbo = ob.get_bbo();
                const auto b = Clock::now();
                ns_bbo += std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
                ++n_bbo;
                checksum += (uint64_t)bbo.bid_qty + (uint64_t)bbo.ask_qty;
                break;
            }
        }
        if ((i + 1) % 100 == 0) {
            const auto bids = ob.get_bids(10);
            const auto asks = ob.get_asks(10);
            for (const auto& l : bids) checksum += (uint64_t)l.qty;
            for (const auto& l : asks) checksum += (uint64_t)l.qty;
        }
    }
    const auto t_end = Clock::now();

    RunMetrics m;
    m.total_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    m.ns_add = n_add ? (double)ns_add / n_add : 0.0;
    m.ns_cancel = n_cancel ? (double)ns_cancel / n_cancel : 0.0;
    m.ns_bbo = n_bbo ? (double)ns_bbo / n_bbo : 0.0;
    m.checksum = checksum;
    m.final_bbo = ob.get_bbo();
    return m;
}

static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

template <typename OB>
static int benchmark(const char* name, const std::vector<Op>& ops, int runs) {
    std::vector<double> totals, adds, cancels, bbos;
    uint64_t checksum = 0;
    BBO final_bbo;
    for (int r = 0; r < runs; ++r) {
        RunMetrics m = run_once<OB>(ops);
        totals.push_back(m.total_ms);
        adds.push_back(m.ns_add);
        cancels.push_back(m.ns_cancel);
        bbos.push_back(m.ns_bbo);
        checksum = m.checksum;    // identical every run (same workload)
        final_bbo = m.final_bbo;
        std::printf("  run %d: total=%.2f ms  add=%.1f ns  cancel=%.1f ns  bbo=%.1f ns\n",
                    r + 1, m.total_ms, m.ns_add, m.ns_cancel, m.ns_bbo);
    }

    std::printf("\nImplementation %s  |  ops=%zu  runs=%d  (median)\n", name, ops.size(), runs);
    std::printf("  total time : %.2f ms\n", median(totals));
    std::printf("  ns / add   : %.1f\n", median(adds));
    std::printf("  ns / cancel: %.1f\n", median(cancels));
    std::printf("  ns / bbo   : %.1f\n", median(bbos));

    std::printf("\nFinal BBO:\n");
    if (final_bbo.has_bid)
        std::printf("  Bid: %.2f x %ld\n", final_bbo.bid_price, final_bbo.bid_qty);
    else
        std::printf("  Bid: (none)\n");
    if (final_bbo.has_ask)
        std::printf("  Ask: %.2f x %ld\n", final_bbo.ask_price, final_bbo.ask_qty);
    else
        std::printf("  Ask: (none)\n");
    std::printf("Checksum: %llu\n", (unsigned long long)checksum);
    return 0;
}

// ---------------------------------------------------------------------------
// Correctness
// ---------------------------------------------------------------------------
static bool bbo_eq(const BBO& x, const BBO& y) {
    return x.has_bid == y.has_bid && x.bid_price == y.bid_price && x.bid_qty == y.bid_qty &&
           x.has_ask == y.has_ask && x.ask_price == y.ask_price && x.ask_qty == y.ask_qty;
}

static bool levels_eq(const std::vector<Level>& x, const std::vector<Level>& y) {
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); ++i)
        if (x[i].price != y[i].price || x[i].qty != y[i].qty) return false;
    return true;
}

// Replay the workload through all three at once, comparing after every op.
static bool verify_agreement(const std::vector<Op>& ops) {
    OrderBookA a;
    OrderBookB b;
    OrderBookC c;
    bool ok = true;
    size_t first_bad = 0;

    for (size_t i = 0; i < ops.size() && ok; ++i) {
        const Op& op = ops[i];
        switch (op.type) {
            case OpType::Add:
                a.add_order(op.id, op.side, op.price, op.qty);
                b.add_order(op.id, op.side, op.price, op.qty);
                c.add_order(op.id, op.side, op.price, op.qty);
                break;
            case OpType::Cancel: {
                const bool ra = a.cancel_order(op.id);
                const bool rb = b.cancel_order(op.id);
                const bool rc = c.cancel_order(op.id);
                if (ra != rb || rb != rc) { ok = false; first_bad = i; }
                break;
            }
            case OpType::Bbo:
                break;
        }
        if (ok) {
            const BBO ba = a.get_bbo(), bb = b.get_bbo(), bc = c.get_bbo();
            if (!bbo_eq(ba, bb) || !bbo_eq(bb, bc)) { ok = false; first_bad = i; }
        }
        if (ok && (i + 1) % 100 == 0) {
            if (!levels_eq(a.get_bids(10), b.get_bids(10)) ||
                !levels_eq(b.get_bids(10), c.get_bids(10)) ||
                !levels_eq(a.get_asks(10), b.get_asks(10)) ||
                !levels_eq(b.get_asks(10), c.get_asks(10))) {
                ok = false;
                first_bad = i;
            }
        }
    }

    if (!ok)
        std::printf("  MISMATCH at operation index %zu\n", first_bad);
    return ok;
}

// Spec's "Required Behavior" edge cases, checked on each implementation.
template <typename OB>
static bool required_behaviors(const char* name) {
    bool ok = true;

    OB empty;
    BBO e = empty.get_bbo();
    if (e.has_bid || e.has_ask) { std::printf("  [%s] empty-book BBO wrong\n", name); ok = false; }
    if (!empty.get_bids(10).empty() || !empty.get_asks(10).empty()) {
        std::printf("  [%s] empty-book depth wrong\n", name); ok = false;
    }

    OB ob;
    if (ob.cancel_order(123456)) { std::printf("  [%s] cancel-unknown should be false\n", name); ok = false; }
    ob.add_order(1, Side::Buy, 99.98, 10);
    if (!ob.cancel_order(1)) { std::printf("  [%s] cancel-live should be true\n", name); ok = false; }
    if (ob.cancel_order(1)) { std::printf("  [%s] double-cancel should be false\n", name); ok = false; }

    OB three;
    three.add_order(10, Side::Buy, 99.98, 5);
    three.add_order(11, Side::Buy, 99.97, 5);
    three.add_order(12, Side::Buy, 99.96, 5);
    if (three.get_bids(10).size() != 3) {
        std::printf("  [%s] insufficient-depth should return 3\n", name); ok = false;
    }
    return ok;
}

static int run_verify(const std::vector<Op>& ops) {
    std::printf("Running correctness test...\n\n");

    const bool ra = required_behaviors<OrderBookA>("A");
    const bool rb = required_behaviors<OrderBookB>("B");
    const bool rc = required_behaviors<OrderBookC>("C");
    const bool agree = verify_agreement(ops);

    std::printf("Implementation A: %s\n", (ra && agree) ? "PASS" : "FAIL");
    std::printf("Implementation B: %s\n", (rb && agree) ? "PASS" : "FAIL");
    std::printf("Implementation C: %s\n", (rc && agree) ? "PASS" : "FAIL");
    std::printf("\n");
    if (ra && rb && rc && agree) {
        std::printf("All implementations produce identical results.\n");
        return 0;
    }
    std::printf("Implementations DO NOT agree.\n");
    return 1;
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    std::string impl;
    bool verify = false;
    size_t ops_n = 1000000;
    int runs = 5;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--verify") == 0) {
            verify = true;
        } else if (std::strcmp(argv[i], "--impl") == 0 && i + 1 < argc) {
            impl = argv[++i];
        } else if (std::strcmp(argv[i], "--ops") == 0 && i + 1 < argc) {
            ops_n = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--runs") == 0 && i + 1 < argc) {
            runs = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "unknown or incomplete argument: %s\n", argv[i]);
            std::fprintf(stderr, "usage: %s [--verify] [--impl A|B|C] [--ops N] [--runs R]\n", argv[0]);
            return 2;
        }
    }

    const std::vector<Op> ops = generate_workload(ops_n);

    if (verify) return run_verify(ops);

    if (impl == "A") return benchmark<OrderBookA>("A", ops, runs);
    if (impl == "B") return benchmark<OrderBookB>("B", ops, runs);
    if (impl == "C") return benchmark<OrderBookC>("C", ops, runs);

    std::fprintf(stderr, "usage: %s [--verify] [--impl A|B|C] [--ops N] [--runs R]\n", argv[0]);
    return 2;
}
