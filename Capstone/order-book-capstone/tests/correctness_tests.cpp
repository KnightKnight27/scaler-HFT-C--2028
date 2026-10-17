// =============================================================================
// correctness_tests.cpp  –  Automated correctness verification
// =============================================================================
// Compile:
//   g++ -std=c++17 -O2 -I../include correctness_tests.cpp
//       ../src/map_order_book.cpp ../src/vector_order_book.cpp
//       ../src/array_order_book.cpp -o correctness_tests
// Run:
//   ./correctness_tests
// =============================================================================

#include "order_book.hpp"
#include "map_order_book.hpp"
#include "vector_order_book.hpp"
#include "array_order_book.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <functional>
#include <cassert>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

using namespace hft;

// ---------------------------------------------------------------------------
// Lightweight test harness
// ---------------------------------------------------------------------------
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                          \
    do {                                                          \
        if (!(cond)) {                                            \
            std::cerr << "  FAIL: " << (msg) << "\n";            \
            ++g_fail;                                             \
        } else {                                                  \
            ++g_pass;                                             \
        }                                                         \
    } while (false)

#define CHECK_THROW(expr, exc_type, msg)                          \
    do {                                                          \
        bool caught = false;                                      \
        try { (expr); }                                           \
        catch (const exc_type&) { caught = true; }               \
        catch (...) {}                                            \
        CHECK(caught, msg);                                       \
    } while (false)

#define CHECK_NOTHROW(expr, msg)                                  \
    do {                                                          \
        bool ok = true;                                           \
        try { (expr); }                                           \
        catch (...) { ok = false; }                               \
        CHECK(ok, msg);                                           \
    } while (false)

// Floating-point equality with 0.001 tolerance (1/10 tick)
static bool feq(double a, double b) { return std::fabs(a - b) < 0.001; }

// ---------------------------------------------------------------------------
// Run all tests against ONE implementation
// ---------------------------------------------------------------------------
static void run_tests(IOrderBook& book, const std::string& name)
{
    std::cout << "\n=== Testing: " << name << " ===\n";

    // -----------------------------------------------------------------------
    // T1: Empty book
    // -----------------------------------------------------------------------
    {
        BBO b = book.get_bbo();
        CHECK(!b.has_bid,  "T1: empty book has no bid");
        CHECK(!b.has_ask,  "T1: empty book has no ask");
        auto bids = book.get_bids(10);
        auto asks = book.get_asks(10);
        CHECK(bids.empty(), "T1: empty book get_bids is empty");
        CHECK(asks.empty(), "T1: empty book get_asks is empty");
    }

    // -----------------------------------------------------------------------
    // T2: One bid, one ask
    // -----------------------------------------------------------------------
    book.add_order(1, Side::Buy,  99.99, 10);
    book.add_order(2, Side::Sell, 100.01,  5);
    {
        BBO b = book.get_bbo();
        CHECK(b.has_bid,          "T2: has bid");
        CHECK(feq(b.bid_price, 99.99), "T2: bid price");
        CHECK(b.bid_qty == 10,    "T2: bid qty");
        CHECK(b.has_ask,          "T2: has ask");
        CHECK(feq(b.ask_price, 100.01), "T2: ask price");
        CHECK(b.ask_qty == 5,     "T2: ask qty");
    }

    // -----------------------------------------------------------------------
    // T3: Multiple orders at same price level
    // -----------------------------------------------------------------------
    book.add_order(3, Side::Buy, 99.99, 20);
    {
        BBO b = book.get_bbo();
        CHECK(b.bid_qty == 30, "T3: aggregated bid qty = 30");
    }

    // -----------------------------------------------------------------------
    // T4: Depth queries, bids descending, asks ascending
    // -----------------------------------------------------------------------
    book.add_order(4, Side::Buy, 99.00, 50);
    book.add_order(5, Side::Buy, 98.00, 15);
    book.add_order(6, Side::Sell, 101.00, 8);
    {
        auto bids = book.get_bids(3);
        CHECK(bids.size() == 3,            "T4: 3 bid levels");
        CHECK(feq(bids[0].price, 99.99),   "T4: bid[0] price");
        CHECK(feq(bids[1].price, 99.00),   "T4: bid[1] price");
        CHECK(feq(bids[2].price, 98.00),   "T4: bid[2] price");

        auto asks = book.get_asks(2);
        CHECK(asks.size() == 2,            "T4: 2 ask levels");
        CHECK(feq(asks[0].price, 100.01),  "T4: ask[0] price");
        CHECK(feq(asks[1].price, 101.00),  "T4: ask[1] price");
    }

    // -----------------------------------------------------------------------
    // T5: Depth of zero / negative
    // -----------------------------------------------------------------------
    {
        CHECK(book.get_bids(0).empty(),  "T5: depth=0 bids empty");
        CHECK(book.get_asks(0).empty(),  "T5: depth=0 asks empty");
        CHECK(book.get_bids(-1).empty(), "T5: depth=-1 bids empty");
        CHECK(book.get_asks(-1).empty(), "T5: depth=-1 asks empty");
    }

    // -----------------------------------------------------------------------
    // T6: Depth greater than available levels
    // -----------------------------------------------------------------------
    {
        auto bids = book.get_bids(100);
        CHECK(bids.size() == 3, "T6: depth=100 returns all 3 bid levels");
    }

    // -----------------------------------------------------------------------
    // T7: Cancel one of several orders at a level – qty decreases
    // -----------------------------------------------------------------------
    {
        bool r = book.cancel_order(3);
        CHECK(r, "T7: cancel returns true");
        BBO b = book.get_bbo();
        CHECK(b.bid_qty == 10, "T7: bid qty reduced to 10 after cancel");
        CHECK(feq(b.bid_price, 99.99), "T7: best bid still 99.99");
    }

    // -----------------------------------------------------------------------
    // T8: Cancel final order at a level – level disappears
    // -----------------------------------------------------------------------
    {
        bool r = book.cancel_order(1);
        CHECK(r, "T8: cancel final order at 99.99 succeeds");
        BBO b = book.get_bbo();
        CHECK(b.has_bid,          "T8: still has bids");
        CHECK(feq(b.bid_price, 99.00), "T8: best bid moves to 99.00");
    }

    // -----------------------------------------------------------------------
    // T9: Cancel the current best bid
    // -----------------------------------------------------------------------
    {
        book.cancel_order(4);  // 99.00
        BBO b = book.get_bbo();
        CHECK(feq(b.bid_price, 98.00), "T9: best bid moves to 98.00 after best cancel");
    }

    // -----------------------------------------------------------------------
    // T10: Cancel the current best ask
    // -----------------------------------------------------------------------
    {
        book.cancel_order(2); // 100.01
        BBO b = book.get_bbo();
        CHECK(b.has_ask, "T10: still has asks");
        CHECK(feq(b.ask_price, 101.00), "T10: best ask moves to 101.00");
    }

    // -----------------------------------------------------------------------
    // T11: Cancel unknown ID → false
    // -----------------------------------------------------------------------
    {
        bool r = book.cancel_order(9999);
        CHECK(!r, "T11: unknown id → false");
    }

    // -----------------------------------------------------------------------
    // T12: Repeated cancellation → false
    // -----------------------------------------------------------------------
    {
        book.cancel_order(5); // clears 98.00
        bool r = book.cancel_order(5); // already cancelled
        CHECK(!r, "T12: repeated cancel → false");
    }

    // -----------------------------------------------------------------------
    // T13: Empty book after all cancellations
    // -----------------------------------------------------------------------
    {
        book.cancel_order(6); // clears 101.00
        BBO b = book.get_bbo();
        CHECK(!b.has_bid, "T13: empty bid after all cancels");
        CHECK(!b.has_ask, "T13: empty ask after all cancels");
    }

    // -----------------------------------------------------------------------
    // T14: Boundary prices
    // -----------------------------------------------------------------------
    {
        book.add_order(100, Side::Buy,   50.00, 1);
        book.add_order(101, Side::Sell, 150.00, 1);
        BBO b = book.get_bbo();
        CHECK(feq(b.bid_price,  50.00), "T14: lowest valid bid");
        CHECK(feq(b.ask_price, 150.00), "T14: highest valid ask");
        book.cancel_order(100);
        book.cancel_order(101);
    }

    // -----------------------------------------------------------------------
    // T15: Rejection of midpoint price
    // -----------------------------------------------------------------------
    {
        CHECK_THROW(book.add_order(200, Side::Buy,  100.00, 1),
                    std::invalid_argument, "T15: midpoint rejected as bid");
        CHECK_THROW(book.add_order(201, Side::Sell, 100.00, 1),
                    std::invalid_argument, "T15: midpoint rejected as ask");
    }

    // -----------------------------------------------------------------------
    // T16: Invalid price ticks (bid price in ask range, vice versa)
    // -----------------------------------------------------------------------
    {
        CHECK_THROW(book.add_order(202, Side::Buy,  100.01, 1),
                    std::invalid_argument, "T16: ask price as bid rejected");
        CHECK_THROW(book.add_order(203, Side::Sell, 99.99, 1),
                    std::invalid_argument, "T16: bid price as ask rejected");
        CHECK_THROW(book.add_order(204, Side::Buy,  49.99, 1),
                    std::invalid_argument, "T16: below bid min rejected");
        CHECK_THROW(book.add_order(205, Side::Sell, 150.01, 1),
                    std::invalid_argument, "T16: above ask max rejected");
    }

    // -----------------------------------------------------------------------
    // T17: Duplicate order IDs
    // -----------------------------------------------------------------------
    {
        book.add_order(300, Side::Buy, 75.00, 10);
        CHECK_THROW(book.add_order(300, Side::Buy, 75.00, 5),
                    std::invalid_argument, "T17: duplicate id rejected");
        book.cancel_order(300);
    }

    // -----------------------------------------------------------------------
    // T18: Invalid quantities
    // -----------------------------------------------------------------------
    {
        CHECK_THROW(book.add_order(400, Side::Buy, 75.00,  0),
                    std::invalid_argument, "T18: qty=0 rejected");
        CHECK_THROW(book.add_order(401, Side::Buy, 75.00, -1),
                    std::invalid_argument, "T18: negative qty rejected");
    }

    // -----------------------------------------------------------------------
    // T19: Randomized operation sequence – cross-check internal consistency
    // -----------------------------------------------------------------------
    {
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> bid_tick_dist(TICK_BID_MIN, TICK_BID_MAX);
        std::uniform_int_distribution<int> ask_tick_dist(TICK_ASK_MIN, TICK_ASK_MAX);
        std::uniform_int_distribution<int> qty_dist(1, 100);
        std::uniform_int_distribution<int> side_dist(0, 1);

        std::vector<std::uint64_t> live_ids;
        std::uint64_t next_id = 10000;

        // 500 adds
        for (int i = 0; i < 500; ++i) {
            Side s = side_dist(rng) ? Side::Buy : Side::Sell;
            int tick = (s == Side::Buy) ? bid_tick_dist(rng) : ask_tick_dist(rng);
            long q   = qty_dist(rng);
            CHECK_NOTHROW(book.add_order(next_id, s, tick_to_price(tick), q),
                          "T19: random add");
            live_ids.push_back(next_id++);
        }

        // 200 cancels of live orders
        std::uniform_int_distribution<std::size_t> id_dist(0, live_ids.size() - 1);
        for (int i = 0; i < 200; ++i) {
            std::size_t idx = id_dist(rng) % live_ids.size();
            std::uint64_t cid = live_ids[idx];
            live_ids.erase(live_ids.begin() + static_cast<std::ptrdiff_t>(idx));
            bool r = book.cancel_order(cid);
            CHECK(r, "T19: cancel live order succeeds");
        }

        // BBO consistency
        BBO b = book.get_bbo();
        if (b.has_bid) CHECK(b.bid_qty > 0, "T19: bid qty positive");
        if (b.has_ask) CHECK(b.ask_qty > 0, "T19: ask qty positive");

        // Cancel remaining
        for (auto id : live_ids) book.cancel_order(id);
        BBO b2 = book.get_bbo();
        CHECK(!b2.has_bid, "T19: all cancelled → no bid");
        CHECK(!b2.has_ask, "T19: all cancelled → no ask");
    }

    // -----------------------------------------------------------------------
    // T20: Repeated add/cancel cycle does not accumulate stale state
    // -----------------------------------------------------------------------
    {
        for (int cycle = 0; cycle < 50; ++cycle) {
            book.add_order(20000 + cycle, Side::Buy,  80.00, 10);
            book.add_order(20050 + cycle, Side::Sell, 120.00, 10);
            book.cancel_order(20000 + cycle);
            book.cancel_order(20050 + cycle);
        }
        BBO b = book.get_bbo();
        CHECK(!b.has_bid, "T20: repeated cycle leaves empty book (bid)");
        CHECK(!b.has_ask, "T20: repeated cycle leaves empty book (ask)");
    }

    // -----------------------------------------------------------------------
    // T21: Non-finite prices (NaN, +Inf, -Inf) must be rejected
    // -----------------------------------------------------------------------
    {
        const double nan_v  = std::numeric_limits<double>::quiet_NaN();
        const double inf_v  = std::numeric_limits<double>::infinity();
        const double ninf_v = -std::numeric_limits<double>::infinity();

        BBO before = book.get_bbo();

        CHECK_THROW(book.add_order(5000, Side::Buy,  nan_v,  1),
                    std::invalid_argument, "T21: NaN bid rejected");
        CHECK_THROW(book.add_order(5001, Side::Sell, nan_v,  1),
                    std::invalid_argument, "T21: NaN ask rejected");
        CHECK_THROW(book.add_order(5002, Side::Buy,  inf_v,  1),
                    std::invalid_argument, "T21: +Inf bid rejected");
        CHECK_THROW(book.add_order(5003, Side::Sell, inf_v,  1),
                    std::invalid_argument, "T21: +Inf ask rejected");
        CHECK_THROW(book.add_order(5004, Side::Buy,  ninf_v, 1),
                    std::invalid_argument, "T21: -Inf bid rejected");
        CHECK_THROW(book.add_order(5005, Side::Sell, ninf_v, 1),
                    std::invalid_argument, "T21: -Inf ask rejected");

        // Book must be unchanged after all rejections
        BBO after = book.get_bbo();
        CHECK(before.has_bid == after.has_bid, "T21: book unchanged after NaN/Inf (bid flag)");
        CHECK(before.has_ask == after.has_ask, "T21: book unchanged after NaN/Inf (ask flag)");
    }

    // -----------------------------------------------------------------------
    // T22: Off-tick prices must be rejected (not silently rounded)
    // -----------------------------------------------------------------------
    {
        // 99.991 is 0.001 above the 99.99 tick — must not be rounded to 99.99.
        CHECK_THROW(book.add_order(5100, Side::Buy,   99.991, 1),
                    std::invalid_argument, "T22: 99.991 bid rejected as off-tick");
        // 100.009 is 0.009 below the 100.01 tick.
        CHECK_THROW(book.add_order(5101, Side::Sell, 100.009, 1),
                    std::invalid_argument, "T22: 100.009 ask rejected as off-tick");
        // Midpoint with fractional: clearly not a valid tick on either side.
        CHECK_THROW(book.add_order(5102, Side::Buy,   75.005, 1),
                    std::invalid_argument, "T22: 75.005 rejected as off-tick");
        CHECK_THROW(book.add_order(5103, Side::Sell, 125.001, 1),
                    std::invalid_argument, "T22: 125.001 rejected as off-tick");

        // Boundary prices expressed exactly must be accepted.
        CHECK_NOTHROW(book.add_order(5104, Side::Buy,   99.99, 1), "T22: 99.99 accepted");
        CHECK_NOTHROW(book.add_order(5105, Side::Sell, 100.01, 1), "T22: 100.01 accepted");
        book.cancel_order(5104);
        book.cancel_order(5105);
    }

    // -----------------------------------------------------------------------
    // T23: Rejected operations leave the book state unchanged
    // -----------------------------------------------------------------------
    {
        // Set up a known baseline
        book.add_order(6000, Side::Buy,  80.00, 42);
        book.add_order(6001, Side::Sell, 120.00, 17);
        BBO baseline = book.get_bbo();
        auto baseline_bids = book.get_bids(10);
        auto baseline_asks = book.get_asks(10);

        // Attempt several invalid operations; each must throw and leave state intact.
        try { book.add_order(6002, Side::Buy, 99.991, 5); } catch (...) {}
        try { book.add_order(6002, Side::Buy, std::numeric_limits<double>::quiet_NaN(), 1); } catch (...) {}
        try { book.add_order(6002, Side::Buy, 100.00, 1); } catch (...) {}   // midpoint
        try { book.add_order(6000, Side::Buy, 80.00,  1); } catch (...) {}   // duplicate ID
        try { book.add_order(6003, Side::Buy, 80.00,  0); } catch (...) {}   // qty=0

        BBO after = book.get_bbo();
        auto after_bids = book.get_bids(10);
        auto after_asks = book.get_asks(10);

        CHECK(baseline.has_bid == after.has_bid,   "T23: bid flag unchanged");
        CHECK(baseline.has_ask == after.has_ask,   "T23: ask flag unchanged");
        if (baseline.has_bid && after.has_bid) {
            CHECK(std::fabs(baseline.bid_price - after.bid_price) < 0.001, "T23: bid price unchanged");
            CHECK(baseline.bid_qty == after.bid_qty,  "T23: bid qty unchanged");
        }
        if (baseline.has_ask && after.has_ask) {
            CHECK(std::fabs(baseline.ask_price - after.ask_price) < 0.001, "T23: ask price unchanged");
            CHECK(baseline.ask_qty == after.ask_qty,  "T23: ask qty unchanged");
        }
        CHECK(after_bids.size() == baseline_bids.size(), "T23: bid depth unchanged");
        CHECK(after_asks.size() == baseline_asks.size(), "T23: ask depth unchanged");

        book.cancel_order(6000);
        book.cancel_order(6001);
    }

    // -----------------------------------------------------------------------
    // T24: Quantity overflow – adding one more unit must throw std::overflow_error
    //      and leave the aggregate quantity unchanged.
    // -----------------------------------------------------------------------
    {
        // Fill a bid level to LONG_MAX using a single order, then try +1.
        const long LMAX = std::numeric_limits<long>::max();
        book.add_order(7000, Side::Buy,  70.00, LMAX);
        book.add_order(7001, Side::Sell, 130.00, LMAX);

        long bid_qty_before = book.get_bbo().bid_qty;
        long ask_qty_before = book.get_bbo().ask_qty;

        CHECK_THROW(book.add_order(7002, Side::Buy,  70.00, 1),
                    std::overflow_error, "T24: bid overflow throws std::overflow_error");
        CHECK_THROW(book.add_order(7003, Side::Sell, 130.00, 1),
                    std::overflow_error, "T24: ask overflow throws std::overflow_error");

        // Quantity must be exactly the same after the rejected additions.
        CHECK(book.get_bbo().bid_qty == bid_qty_before, "T24: bid qty unchanged after overflow rejection");
        CHECK(book.get_bbo().ask_qty == ask_qty_before, "T24: ask qty unchanged after overflow rejection");

        // IDs used in rejected calls must NOT have been inserted into the book.
        CHECK(!book.cancel_order(7002), "T24: rejected order id 7002 not in book");
        CHECK(!book.cancel_order(7003), "T24: rejected order id 7003 not in book");

        book.cancel_order(7000);
        book.cancel_order(7001);
    }
}

// ---------------------------------------------------------------------------
// Cross-implementation verifier
// ---------------------------------------------------------------------------
// Generates a deterministic operation stream with seed 42, applies it to all
// three implementations, and checks that observable results match.
// ---------------------------------------------------------------------------

struct Op {
    enum Type { ADD, CANCEL, BBO_QUERY } type;
    std::uint64_t id;
    Side          side;
    double        price;
    long          qty;
};

static std::vector<Op> generate_ops(int n_ops, std::uint64_t& next_id,
                                    std::vector<std::uint64_t>& live_ids,
                                    std::mt19937& rng)
{
    std::uniform_int_distribution<int> bid_tick(TICK_BID_MIN, TICK_BID_MAX);
    std::uniform_int_distribution<int> ask_tick(TICK_ASK_MIN, TICK_ASK_MAX);
    std::uniform_int_distribution<int> qty_d(1, 100);
    std::uniform_int_distribution<int> side_d(0, 1);
    std::uniform_int_distribution<int> op_d(0, 9);

    std::vector<Op> ops;
    ops.reserve(n_ops);
    for (int i = 0; i < n_ops; ++i) {
        int r = op_d(rng);
        if (r < 7 || live_ids.empty()) {
            // ADD
            Op op;
            op.type  = Op::ADD;
            op.id    = next_id++;
            op.side  = side_d(rng) ? Side::Buy : Side::Sell;
            int tick = (op.side == Side::Buy) ? bid_tick(rng) : ask_tick(rng);
            op.price = tick_to_price(tick);
            op.qty   = qty_d(rng);
            live_ids.push_back(op.id);
            ops.push_back(op);
        } else if (r < 9 && !live_ids.empty()) {
            // CANCEL a live order
            std::uniform_int_distribution<std::size_t> id_d(0, live_ids.size()-1);
            std::size_t idx = id_d(rng);
            Op op;
            op.type = Op::CANCEL;
            op.id   = live_ids[idx];
            live_ids.erase(live_ids.begin() + static_cast<std::ptrdiff_t>(idx));
            ops.push_back(op);
        } else {
            Op op;
            op.type = Op::BBO_QUERY;
            ops.push_back(op);
        }
    }
    return ops;
}

static bool levels_equal(const std::vector<Level>& a, const std::vector<Level>& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!feq(a[i].price, b[i].price)) return false;
        if (a[i].qty != b[i].qty) return false;
    }
    return true;
}

static bool bbo_equal(const BBO& a, const BBO& b)
{
    if (a.has_bid != b.has_bid) return false;
    if (a.has_ask != b.has_ask) return false;
    if (a.has_bid && (!feq(a.bid_price, b.bid_price) || a.bid_qty != b.bid_qty)) return false;
    if (a.has_ask && (!feq(a.ask_price, b.ask_price) || a.ask_qty != b.ask_qty)) return false;
    return true;
}

static void run_cross_verify()
{
    std::cout << "\n=== Cross-Implementation Verifier ===\n";

    MapOrderBook    ma;
    VectorOrderBook vb;
    ArrayOrderBook  ac;

    std::mt19937 rng(42);
    std::uint64_t next_id = 1;
    std::vector<std::uint64_t> live_ids;

    std::vector<Op> ops = generate_ops(5000, next_id, live_ids, rng);

    int mismatches = 0;

    for (std::size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];

        if (op.type == Op::ADD) {
            bool ta = true, tb = true, tc = true;
            try { ma.add_order(op.id, op.side, op.price, op.qty); } catch (...) { ta = false; }
            try { vb.add_order(op.id, op.side, op.price, op.qty); } catch (...) { tb = false; }
            try { ac.add_order(op.id, op.side, op.price, op.qty); } catch (...) { tc = false; }
            if (ta != tb || tb != tc) {
                std::cerr << "  MISMATCH op " << i << " ADD id=" << op.id
                          << " A=" << ta << " B=" << tb << " C=" << tc << "\n";
                ++mismatches;
            }
        } else if (op.type == Op::CANCEL) {
            bool ra = ma.cancel_order(op.id);
            bool rb = vb.cancel_order(op.id);
            bool rc = ac.cancel_order(op.id);
            if (ra != rb || rb != rc) {
                std::cerr << "  MISMATCH op " << i << " CANCEL id=" << op.id
                          << " A=" << ra << " B=" << rb << " C=" << rc << "\n";
                ++mismatches;
            }
        }

        // After every op compare BBO and depth
        BBO ba = ma.get_bbo(), bb = vb.get_bbo(), bc = ac.get_bbo();
        if (!bbo_equal(ba, bb) || !bbo_equal(bb, bc)) {
            std::cerr << "  MISMATCH BBO at op " << i << "\n";
            std::cerr << "    A: bid=" << ba.has_bid << " " << ba.bid_price << "x" << ba.bid_qty
                      << " ask=" << ba.has_ask << " " << ba.ask_price << "x" << ba.ask_qty << "\n";
            std::cerr << "    B: bid=" << bb.has_bid << " " << bb.bid_price << "x" << bb.bid_qty
                      << " ask=" << bb.has_ask << " " << bb.ask_price << "x" << bb.ask_qty << "\n";
            std::cerr << "    C: bid=" << bc.has_bid << " " << bc.bid_price << "x" << bc.bid_qty
                      << " ask=" << bc.has_ask << " " << bc.ask_price << "x" << bc.ask_qty << "\n";
            ++mismatches;
        }

        auto bids_a = ma.get_bids(10), bids_b = vb.get_bids(10), bids_c = ac.get_bids(10);
        if (!levels_equal(bids_a, bids_b) || !levels_equal(bids_b, bids_c)) {
            std::cerr << "  MISMATCH bids at op " << i << "\n";
            ++mismatches;
        }
        auto asks_a = ma.get_asks(10), asks_b = vb.get_asks(10), asks_c = ac.get_asks(10);
        if (!levels_equal(asks_a, asks_b) || !levels_equal(asks_b, asks_c)) {
            std::cerr << "  MISMATCH asks at op " << i << "\n";
            ++mismatches;
        }
    }

    if (mismatches == 0)
        std::cout << "  Cross-verify PASS (" << ops.size() << " ops, all implementations agree)\n";
    else
        std::cout << "  Cross-verify FAIL (" << mismatches << " mismatches in "
                  << ops.size() << " ops)\n";

    g_fail += mismatches;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    {
        MapOrderBook book;
        run_tests(book, "MapOrderBook (A)");
    }
    {
        VectorOrderBook book;
        run_tests(book, "VectorOrderBook (B)");
    }
    {
        ArrayOrderBook book;
        run_tests(book, "ArrayOrderBook (C)");
    }

    run_cross_verify();

    std::cout << "\n================================================\n";
    std::cout << "PASS: " << g_pass << "   FAIL: " << g_fail << "\n";
    if (g_fail == 0)
        std::cout << "Overall result: PASS\n";
    else
        std::cout << "Overall result: FAIL\n";
    std::cout << "================================================\n";

    return g_fail == 0 ? 0 : 1;
}
