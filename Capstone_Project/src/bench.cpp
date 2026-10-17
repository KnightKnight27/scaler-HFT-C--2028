// =============================================================================
// bench.cpp — Unified benchmark and correctness-verification program
// =============================================================================
//
// Usage:
//   ./bench --impl A          Run benchmark using Implementation A
//   ./bench --impl B          Run benchmark using Implementation B
//   ./bench --impl C          Run benchmark using Implementation C
//   ./bench --verify          Run correctness check across all three
//
// Workload (benchmark):
//   1 000 000 total operations, fixed seed 42.
//   ~70% add_order, ~20% cancel_order, ~10% get_bbo
//   Every 100 ops: get_bids(10) + get_asks(10)
//
// Each benchmark is run 5 times; the median is reported.
// =============================================================================

#include "order_book_a.h"
#include "order_book_b.h"
#include "order_book_c.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <climits>

// ---------------------------------------------------------------------------
// Workload generator
// ---------------------------------------------------------------------------

struct OpRecord {
    enum Type { ADD, CANCEL, BBO } type = ADD;
    bool     is_depth  = false;   // true when this BBO op is actually a depth query
    // ADD fields
    uint64_t id        = 0;
    Side     side      = Side::Buy;
    int      tick      = 0;
    long     qty       = 0;
    // CANCEL field
    uint64_t cancel_id = 0;
};

// Pre-build the entire operation sequence so all three impls see identical work.
static std::vector<OpRecord> build_workload(int total_ops, uint32_t seed) {
    std::mt19937 rng(seed);

    // distributions
    std::uniform_int_distribution<int> op_dist(0, 99);          // 0-69→add, 70-89→cancel, 90-99→bbo
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<long> qty_dist(1, 100);
    std::uniform_int_distribution<int> bid_dist(BID_TICK_MIN, BID_TICK_MAX);
    std::uniform_int_distribution<int> ask_dist(ASK_TICK_MIN, ASK_TICK_MAX);

    std::vector<OpRecord> ops;
    ops.reserve(static_cast<size_t>(total_ops));

    // We track live IDs so cancel always selects a live order
    std::vector<uint64_t> live_ids;
    live_ids.reserve(200'000);
    uint64_t next_id = 1;

    for (int i = 0; i < total_ops; ++i) {
        int r = op_dist(rng);

        if (r < 70 || live_ids.empty()) {
            // ADD
            Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            int  tick = (side == Side::Buy) ? bid_dist(rng) : ask_dist(rng);
            long qty  = qty_dist(rng);

            OpRecord op;
            op.type = OpRecord::ADD;
            op.id   = next_id++;
            op.side = side;
            op.tick = tick;
            op.qty  = qty;
            live_ids.push_back(op.id);
            ops.push_back(op);

        } else if (r < 90) {
            // CANCEL — pick a random live order
            std::uniform_int_distribution<size_t> idx_dist(0, live_ids.size() - 1);
            size_t idx = idx_dist(rng);
            uint64_t cid = live_ids[idx];

            // swap-remove so the vector stays compact
            live_ids[idx] = live_ids.back();
            live_ids.pop_back();

            OpRecord op;
            op.type      = OpRecord::CANCEL;
            op.cancel_id = cid;
            ops.push_back(op);

        } else {
            // BBO
            OpRecord op;
            op.type = OpRecord::BBO;
            ops.push_back(op);
        }

        // Every 100 ops, also issue a depth query
        if ((i + 1) % 100 == 0) {
            OpRecord op;
            op.type     = OpRecord::BBO;
            op.is_depth = true;
            ops.push_back(op);
        }
    }

    return ops;
}

// ---------------------------------------------------------------------------
// Generic runner — templated on the order book type
// ---------------------------------------------------------------------------

struct BenchResult {
    long long total_ns;
    double    ns_per_add;
    double    ns_per_cancel;
    double    ns_per_bbo;
    long long checksum;
    BBO       final_bbo;
};

template <typename Book>
BenchResult run_bench(const std::vector<OpRecord>& ops) {
    using Clock = std::chrono::steady_clock;
    using NS    = std::chrono::nanoseconds;

    Book book;

    long long add_ns    = 0, add_cnt    = 0;
    long long cancel_ns = 0, cancel_cnt = 0;
    long long bbo_ns    = 0, bbo_cnt    = 0;
    long long checksum  = 0;

    auto t0 = Clock::now();

    for (const auto& op : ops) {
        if (op.type == OpRecord::ADD) {
            auto s = Clock::now();
            book.add_order(op.id, op.side, tick_to_price(op.tick), op.qty);
            auto e = Clock::now();
            add_ns  += std::chrono::duration_cast<NS>(e - s).count();
            add_cnt += 1;

        } else if (op.type == OpRecord::CANCEL) {
            auto s = Clock::now();
            bool ok = book.cancel_order(op.cancel_id);
            auto e = Clock::now();
            cancel_ns  += std::chrono::duration_cast<NS>(e - s).count();
            cancel_cnt += 1;
            checksum   += ok ? 1 : 0;

        } else {
            if (op.is_depth) {
                // depth query (not timed separately)
                auto bids = book.get_bids(10);
                auto asks = book.get_asks(10);
                for (auto& l : bids) checksum += static_cast<long long>(l.qty);
                for (auto& l : asks) checksum += static_cast<long long>(l.qty);
            } else {
                auto s = Clock::now();
                BBO b = book.get_bbo();
                auto e = Clock::now();
                bbo_ns  += std::chrono::duration_cast<NS>(e - s).count();
                bbo_cnt += 1;
                if (b.has_bid) checksum += static_cast<long long>(b.bid_qty);
                if (b.has_ask) checksum += static_cast<long long>(b.ask_qty);
            }
        }
    }

    auto t1 = Clock::now();
    long long total_ns = std::chrono::duration_cast<NS>(t1 - t0).count();

    BenchResult res;
    res.total_ns      = total_ns;
    res.ns_per_add    = add_cnt    ? static_cast<double>(add_ns)    / add_cnt    : 0.0;
    res.ns_per_cancel = cancel_cnt ? static_cast<double>(cancel_ns) / cancel_cnt : 0.0;
    res.ns_per_bbo    = bbo_cnt    ? static_cast<double>(bbo_ns)    / bbo_cnt    : 0.0;
    res.checksum      = checksum;
    res.final_bbo     = book.get_bbo();
    return res;
}

// ---------------------------------------------------------------------------
// String helpers (used by both unit tests and differential test)
// ---------------------------------------------------------------------------

static std::string level_vec_to_string(const std::vector<Level>& v) {
    std::ostringstream ss;
    for (const auto& l : v)
        ss << std::fixed << std::setprecision(2) << l.price << "x" << l.qty << ";";
    return ss.str();
}

static std::string bbo_to_string(const BBO& b) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    if (b.has_bid) ss << "BID=" << b.bid_price << "x" << b.bid_qty;
    else           ss << "BID=NONE";
    ss << "|";
    if (b.has_ask) ss << "ASK=" << b.ask_price << "x" << b.ask_qty;
    else           ss << "ASK=NONE";
    return ss.str();
}

// ---------------------------------------------------------------------------
// Unit test helpers
// ---------------------------------------------------------------------------

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define EXPECT(cond, msg) do { \
    ++g_tests_run; \
    if (!(cond)) { \
        std::cerr << "  FAIL [" << __LINE__ << "]: " << (msg) << "\n"; \
        ++g_tests_failed; \
    } \
} while(0)

// Run the same named unit test against all three books
template<typename BookA, typename BookB, typename BookC>
struct TripleBook { BookA a; BookB b; BookC c; };

// Helper: apply add to all three
template<typename A, typename B, typename C>
static void tri_add(TripleBook<A,B,C>& tb, uint64_t id, Side side, double price, long qty) {
    tb.a.add_order(id, side, price, qty);
    tb.b.add_order(id, side, price, qty);
    tb.c.add_order(id, side, price, qty);
}

template<typename A, typename B, typename C>
static void tri_cancel(TripleBook<A,B,C>& tb, uint64_t id, bool expected, const char* ctx) {
    bool rA = tb.a.cancel_order(id);
    bool rB = tb.b.cancel_order(id);
    bool rC = tb.c.cancel_order(id);
    ++g_tests_run;
    if (rA != expected || rB != expected || rC != expected) {
        std::cerr << "  FAIL cancel(" << id << ") ctx=" << ctx
                  << " expected=" << expected
                  << " A=" << rA << " B=" << rB << " C=" << rC << "\n";
        ++g_tests_failed;
    }
}

template<typename A, typename B, typename C>
static void tri_expect_bbo(TripleBook<A,B,C>& tb,
                            bool expHasBid, double expBidP, long expBidQ,
                            bool expHasAsk, double expAskP, long expAskQ,
                            const char* ctx) {
    BBO bA = tb.a.get_bbo();
    BBO bB = tb.b.get_bbo();
    BBO bC = tb.c.get_bbo();
    auto check = [&](const BBO& bbo, const char* impl) {
        ++g_tests_run;
        bool ok = true;
        if (bbo.has_bid != expHasBid) ok = false;
        if (expHasBid && std::abs(bbo.bid_price - expBidP) > 0.001) ok = false;
        if (expHasBid && bbo.bid_qty != expBidQ) ok = false;
        if (bbo.has_ask != expHasAsk) ok = false;
        if (expHasAsk && std::abs(bbo.ask_price - expAskP) > 0.001) ok = false;
        if (expHasAsk && bbo.ask_qty != expAskQ) ok = false;
        if (!ok) {
            std::cerr << "  FAIL BBO ctx=" << ctx << " impl=" << impl << "\n"
                      << "    expected has_bid=" << expHasBid << " price=" << expBidP
                      << " qty=" << expBidQ << "\n"
                      << "    got      has_bid=" << bbo.has_bid << " price=" << bbo.bid_price
                      << " qty=" << bbo.bid_qty << "\n"
                      << "    expected has_ask=" << expHasAsk << " price=" << expAskP
                      << " qty=" << expAskQ << "\n"
                      << "    got      has_ask=" << bbo.has_ask << " price=" << bbo.ask_price
                      << " qty=" << bbo.ask_qty << "\n";
            ++g_tests_failed;
        }
    };
    check(bA, "A"); check(bB, "B"); check(bC, "C");
}

// ---------------------------------------------------------------------------
// run_unit_tests — deterministic edge cases
// ---------------------------------------------------------------------------

static void run_unit_tests() {
    std::cout << "--- Unit tests ---\n";

    // ---- 1. Empty book -------------------------------------------------------
    {
        TripleBook<OrderBookA, OrderBookB, OrderBookC> tb;
        tri_expect_bbo(tb, false,0,0, false,0,0, "empty book");

        // get_bids / get_asks on empty book must return empty vectors, not crash
        EXPECT(tb.a.get_bids(10).empty(), "A: get_bids empty book");
        EXPECT(tb.b.get_bids(10).empty(), "B: get_bids empty book");
        EXPECT(tb.c.get_bids(10).empty(), "C: get_bids empty book");
        EXPECT(tb.a.get_asks(10).empty(), "A: get_asks empty book");
        EXPECT(tb.b.get_asks(10).empty(), "B: get_asks empty book");
        EXPECT(tb.c.get_asks(10).empty(), "C: get_asks empty book");
    }

    // ---- 2. Cancel unknown order (never inserted) ----------------------------
    {
        TripleBook<OrderBookA, OrderBookB, OrderBookC> tb;
        tri_cancel(tb, 999, false, "cancel unknown id");
    }

    // ---- 3. Cancel already-cancelled order -----------------------------------
    {
        TripleBook<OrderBookA, OrderBookB, OrderBookC> tb;
        tri_add(tb, 1, Side::Buy, 99.00, 10);
        tri_cancel(tb, 1, true,  "first cancel succeeds");
        tri_cancel(tb, 1, false, "second cancel (already gone) fails");
    }

    // ---- 4. Multiple orders at the same price: aggregate qty -----------------
    {
        TripleBook<OrderBookA, OrderBookB, OrderBookC> tb;
        tri_add(tb, 1, Side::Buy, 99.50, 30);
        tri_add(tb, 2, Side::Buy, 99.50, 70);
        // Both at 99.50 → aggregate qty = 100
        tri_expect_bbo(tb, true, 99.50, 100, false,0,0, "aggregate qty 100");

        // Cancel one → aggregate qty = 70
        tri_cancel(tb, 1, true, "cancel order 1");
        tri_expect_bbo(tb, true, 99.50, 70, false,0,0, "aggregate qty 70 after cancel");

        // Cancel second → book empty
        tri_cancel(tb, 2, true, "cancel order 2");
        tri_expect_bbo(tb, false,0,0, false,0,0, "empty after both cancelled");
    }

    // ---- 5. Best bid updates when top level is cancelled --------------------
    {
        TripleBook<OrderBookA, OrderBookB, OrderBookC> tb;
        tri_add(tb, 1, Side::Buy, 99.99, 10);
        tri_add(tb, 2, Side::Buy, 99.98, 20);
        tri_expect_bbo(tb, true, 99.99, 10, false,0,0, "best bid = 99.99");
        tri_cancel(tb, 1, true, "cancel best bid");
        tri_expect_bbo(tb, true, 99.98, 20, false,0,0, "best bid falls to 99.98");
    }

    // ---- 6. Best ask updates when top level is cancelled --------------------
    {
        TripleBook<OrderBookA, OrderBookB, OrderBookC> tb;
        tri_add(tb, 1, Side::Sell, 100.01, 10);
        tri_add(tb, 2, Side::Sell, 100.02, 20);
        tri_expect_bbo(tb, false,0,0, true, 100.01, 10, "best ask = 100.01");
        tri_cancel(tb, 1, true, "cancel best ask");
        tri_expect_bbo(tb, false,0,0, true, 100.02, 20, "best ask rises to 100.02");
    }

    // ---- 7. Price-band boundaries -------------------------------------------
    {
        TripleBook<OrderBookA, OrderBookB, OrderBookC> tb;
        tri_add(tb, 1, Side::Buy,  50.00,  1);   // min bid
        tri_add(tb, 2, Side::Buy,  99.99,  2);   // max bid
        tri_add(tb, 3, Side::Sell, 100.01, 3);   // min ask
        tri_add(tb, 4, Side::Sell, 150.00, 4);   // max ask
        tri_expect_bbo(tb, true, 99.99, 2, true, 100.01, 3, "boundary prices in BBO");

        auto bidsA = tb.a.get_bids(5000);
        EXPECT(bidsA.size() == 2, "A: 2 bid levels at boundaries");
        EXPECT(std::abs(bidsA[0].price - 99.99) < 0.001, "A: top bid = 99.99");
        EXPECT(std::abs(bidsA[1].price - 50.00) < 0.001, "A: second bid = 50.00");

        auto asksA = tb.a.get_asks(5000);
        EXPECT(asksA.size() == 2, "A: 2 ask levels at boundaries");
        EXPECT(std::abs(asksA[0].price - 100.01) < 0.001, "A: top ask = 100.01");
        EXPECT(std::abs(asksA[1].price - 150.00) < 0.001, "A: second ask = 150.00");
    }

    // ---- 8. Insufficient depth (returns only available levels) --------------
    {
        TripleBook<OrderBookA, OrderBookB, OrderBookC> tb;
        tri_add(tb, 1, Side::Buy, 99.99, 10);
        tri_add(tb, 2, Side::Buy, 99.98, 20);
        tri_add(tb, 3, Side::Buy, 99.97, 30);
        // Only 3 levels exist; requesting 10 must return exactly 3
        EXPECT(tb.a.get_bids(10).size() == 3, "A: get_bids(10) returns 3 when only 3 exist");
        EXPECT(tb.b.get_bids(10).size() == 3, "B: get_bids(10) returns 3 when only 3 exist");
        EXPECT(tb.c.get_bids(10).size() == 3, "C: get_bids(10) returns 3 when only 3 exist");
    }

    // ---- 9. BBO correct when both sides have multiple levels ----------------
    {
        TripleBook<OrderBookA, OrderBookB, OrderBookC> tb;
        tri_add(tb, 1, Side::Buy,  99.50, 50);
        tri_add(tb, 2, Side::Buy,  99.75, 25);  // best bid
        tri_add(tb, 3, Side::Sell, 100.25, 60);
        tri_add(tb, 4, Side::Sell, 100.10, 15);  // best ask
        tri_expect_bbo(tb, true, 99.75, 25, true, 100.10, 15, "bbo with multi levels each side");
    }

    // ---- 10. Cancel mid-book level, BBO unchanged ---------------------------
    {
        TripleBook<OrderBookA, OrderBookB, OrderBookC> tb;
        tri_add(tb, 1, Side::Buy, 99.99, 10);
        tri_add(tb, 2, Side::Buy, 99.90, 20);
        tri_add(tb, 3, Side::Sell, 100.01, 5);
        tri_cancel(tb, 2, true, "cancel non-best bid level");
        // BBO should still be 99.99 / 100.01
        tri_expect_bbo(tb, true, 99.99, 10, true, 100.01, 5, "bbo unchanged after mid-level cancel");
    }

    std::cout << "Unit tests: " << (g_tests_run - g_tests_failed)
              << "/" << g_tests_run << " passed\n\n";
}

// ---------------------------------------------------------------------------
// run_differential_test — 200k-op seeded cross-implementation comparison
// ---------------------------------------------------------------------------

static bool run_differential_test() {
    const int    DIFF_OPS = 200'000;
    const uint32_t SEED   = 42;
    auto ops = build_workload(DIFF_OPS, SEED);

    OrderBookA bookA;
    OrderBookB bookB;
    OrderBookC bookC;

    bool ok = true;
    int mismatch_count = 0;

    for (size_t i = 0; i < ops.size(); ++i) {
        const auto& op = ops[i];

        if (op.type == OpRecord::ADD) {
            double p = tick_to_price(op.tick);
            bookA.add_order(op.id, op.side, p, op.qty);
            bookB.add_order(op.id, op.side, p, op.qty);
            bookC.add_order(op.id, op.side, p, op.qty);

        } else if (op.type == OpRecord::CANCEL) {
            bool rA = bookA.cancel_order(op.cancel_id);
            bool rB = bookB.cancel_order(op.cancel_id);
            bool rC = bookC.cancel_order(op.cancel_id);
            if (rA != rB || rA != rC) {
                std::cout << "  MISMATCH at op " << i << " (cancel): "
                          << "A=" << rA << " B=" << rB << " C=" << rC << "\n";
                ++mismatch_count; ok = false;
            }

        } else {
            if (op.is_depth) {
                auto bidsA = bookA.get_bids(10); auto asksA = bookA.get_asks(10);
                auto bidsB = bookB.get_bids(10); auto asksB = bookB.get_asks(10);
                auto bidsC = bookC.get_bids(10); auto asksC = bookC.get_asks(10);
                std::string sBA = level_vec_to_string(bidsA);
                std::string sBB = level_vec_to_string(bidsB);
                std::string sBC = level_vec_to_string(bidsC);
                std::string sAA = level_vec_to_string(asksA);
                std::string sAB = level_vec_to_string(asksB);
                std::string sAC = level_vec_to_string(asksC);
                if (sBA != sBB || sBA != sBC || sAA != sAB || sAA != sAC) {
                    std::cout << "  MISMATCH at op " << i << " (depth)\n";
                    ++mismatch_count; ok = false;
                }
            } else {
                BBO bA = bookA.get_bbo();
                BBO bB = bookB.get_bbo();
                BBO bC = bookC.get_bbo();
                std::string sA = bbo_to_string(bA);
                std::string sB = bbo_to_string(bB);
                std::string sC = bbo_to_string(bC);
                if (sA != sB || sA != sC) {
                    std::cout << "  MISMATCH at op " << i << " (bbo): "
                              << "A=" << sA << " B=" << sB << " C=" << sC << "\n";
                    ++mismatch_count; ok = false;
                }
            }
        }

        if (mismatch_count >= 5) {
            std::cout << "  Too many mismatches; aborting differential test.\n";
            break;
        }
    }

    return ok;
}

// ---------------------------------------------------------------------------
// run_verify — orchestrate unit + differential tests, exit(1) on failure
// ---------------------------------------------------------------------------

static void run_verify() {
    std::cout << "Running correctness test...\n\n";

    run_unit_tests();

    std::cout << "--- Differential test (200 000 ops, seed=42) ---\n";
    bool diff_ok = run_differential_test();

    std::cout << "\n";
    if (diff_ok) {
        std::cout << "Implementation A: PASS\n";
        std::cout << "Implementation B: PASS\n";
        std::cout << "Implementation C: PASS\n\n";
        std::cout << "All implementations produce identical results.\n";
    } else {
        std::cout << "Differential test: FAIL — implementations disagree.\n";
    }

    bool all_ok = (g_tests_failed == 0) && diff_ok;
    if (!all_ok) {
        std::cerr << "\nVerification FAILED.\n";
        std::exit(1);   // nonzero exit code so CI / graders catch it
    }
}


// ---------------------------------------------------------------------------
// Benchmark driver
// ---------------------------------------------------------------------------

static void print_result(const char* label, const BenchResult& res) {
    std::cout << "\n===== Implementation " << label << " =====\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total time    : " << res.total_ns / 1'000'000.0 << " ms\n";
    std::cout << "ns / add      : " << res.ns_per_add    << " ns\n";
    std::cout << "ns / cancel   : " << res.ns_per_cancel << " ns\n";
    std::cout << "ns / bbo      : " << res.ns_per_bbo    << " ns\n";
    std::cout << "Checksum      : " << res.checksum      << "\n";
    const BBO& b = res.final_bbo;
    std::cout << "Final BBO:\n";
    if (b.has_bid)
        std::cout << "  Bid: " << b.bid_price << " x " << b.bid_qty << "\n";
    else
        std::cout << "  Bid: (empty)\n";
    if (b.has_ask)
        std::cout << "  Ask: " << b.ask_price << " x " << b.ask_qty << "\n";
    else
        std::cout << "  Ask: (empty)\n";
}

template <typename Book>
static void bench_impl(const char* label, const std::vector<OpRecord>& ops) {
    const int RUNS = 5;
    std::vector<BenchResult> results;
    results.reserve(RUNS);
    for (int r = 0; r < RUNS; ++r) {
        results.push_back(run_bench<Book>(ops));
    }
    // sort by total_ns and take median
    std::sort(results.begin(), results.end(),
              [](const BenchResult& a, const BenchResult& b) {
                  return a.total_ns < b.total_ns;
              });
    print_result(label, results[RUNS / 2]);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " --impl A|B|C\n"
                  << "  " << argv[0] << " --verify\n";
        return 1;
    }

    std::string flag = argv[1];

    if (flag == "--verify") {
        run_verify();
        return 0;
    }

    if (flag != "--impl" || argc < 3) {
        std::cerr << "Unknown flag: " << flag << "\n";
        return 1;
    }

    std::string impl = argv[2];
    const int   TOTAL_OPS = 1'000'000;
    const uint32_t SEED   = 42;

    std::cout << "Building workload (" << TOTAL_OPS << " ops, seed=" << SEED << ")...\n";
    auto ops = build_workload(TOTAL_OPS, SEED);
    std::cout << "Workload size: " << ops.size() << " ops (including depth queries)\n";

    if (impl == "A") {
        bench_impl<OrderBookA>("A", ops);
    } else if (impl == "B") {
        bench_impl<OrderBookB>("B", ops);
    } else if (impl == "C") {
        bench_impl<OrderBookC>("C", ops);
    } else {
        std::cerr << "Unknown implementation: " << impl << ". Use A, B, or C.\n";
        return 1;
    }

    return 0;
}
