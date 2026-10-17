// Regression and differential tests for the three order books.
//
//   make test        (or: g++ -std=c++17 -O2 -Iinclude -Isrc tests/test_orderbook.cpp -o test_orderbook)
//
// --verify replays the benchmark workload, which only ever sends valid adds,
// sequential ids and cancels of live orders, into books whose hash tables are
// large enough never to grow. These tests cover what that leaves out:
//   * every one of the 10,000 valid prices through the double API, and
//     rejection of off-tick, out-of-band, non-finite and wrapping prices;
//   * whole-number prices passed as int / long / unsigned;
//   * a full 5,000-level side (depth, ordering, bitmap word boundaries in C);
//   * random 64-bit ids including 0 and 2^64-1, duplicate adds, cancels of
//     unknown or already-cancelled ids, re-use of a cancelled id, invalid
//     prices and quantities, all mixed into a random stream;
//   * implementation C built with a 16-slot hash table, so grow() and the
//     wrap-around case of backward-shift deletion actually run.
// Every result is compared with ReferenceBook (src/reference_book.hpp).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>

#include "book_a_map.hpp"
#include "book_b_vector.hpp"
#include "book_c_array.hpp"
#include "reference_book.hpp"

static int g_checks = 0, g_failures = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) {                                                                \
            ++g_failures;                                                             \
            if (g_failures <= 20) std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                             \
    } while (0)

static bool same(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!(a[i] == b[i])) return false;
    return true;
}

// C starting from its smallest table (16 slots), so a test run grows it about
// a dozen times and wraps probe chains around the end of the table often.
struct SmallTableC : OrderBookC {
    SmallTableC() : OrderBookC(1) {}
};

// ---------------------------------------------------------------------------
// Prices through the double overload
// ---------------------------------------------------------------------------
template <class Book>
static void test_double_prices() {
    Book book;
    uint64_t id = 1;
    // every valid price, written two ways: tick / 100.0 and parsed from text
    for (int t = kBidMinTick; t <= kAskMaxTick; ++t) {
        if (t == 10000) continue;  // the mid price belongs to neither band
        const Side side = t <= kBidMaxTick ? Side::Buy : Side::Sell;
        char text[16];
        std::snprintf(text, sizeof text, "%d.%02d", t / 100, t % 100);
        CHECK(book.add_order(id++, side, t / 100.0, 1));
        CHECK(book.add_order(id++, side, std::strtod(text, nullptr), 2));
    }
    // a full side: 5,000 levels of qty 3, best first, prices exact
    const std::vector<Level> bids = book.get_bids(kLevelsPerSide + 1000);
    const std::vector<Level> asks = book.get_asks(kLevelsPerSide);
    CHECK(bids.size() == static_cast<size_t>(kLevelsPerSide));
    CHECK(asks.size() == static_cast<size_t>(kLevelsPerSide));
    bool ladder_ok = bids.size() == asks.size();
    for (size_t i = 0; ladder_ok && i < bids.size(); ++i) {
        ladder_ok = bids[i].price == to_price(kBidMaxTick - static_cast<int>(i)) && bids[i].qty == 3 &&
                    asks[i].price == to_price(kAskMinTick + static_cast<int>(i)) && asks[i].qty == 3;
    }
    CHECK(ladder_ok);
    const BBO b = book.get_bbo();
    CHECK(b.has_bid && b.bid_price == 99.99 && b.bid_qty == 3);
    CHECK(b.has_ask && b.ask_price == 100.01 && b.ask_qty == 3);

    // invalid prices are rejected and change nothing
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(!book.add_order(id++, Side::Buy, 42949772.95, 5));  // 2^32 + 9999 ticks
    CHECK(!book.add_order(id++, Side::Buy, -42949572.97, 5)); // wraps to a valid tick from below
    CHECK(!book.add_order(id++, Side::Sell, 100.013, 5));
    CHECK(!book.add_order(id++, Side::Buy, 99.995, 5));
    CHECK(!book.add_order(id++, Side::Buy, nan, 5));
    CHECK(!book.add_order(id++, Side::Sell, inf, 5));
    CHECK(!book.add_order(id++, Side::Buy, -inf, 5));
    CHECK(!book.add_order(id++, Side::Buy, 1e300, 5));
    CHECK(!book.add_order(id++, Side::Buy, 49.99, 5));
    CHECK(!book.add_order(id++, Side::Sell, 150.01, 5));
    CHECK(!book.add_order(id++, Side::Buy, 100.00, 5));
    CHECK(!book.add_order(id++, Side::Sell, 100.00, 5));
    CHECK(!book.add_order(id++, Side::Buy, 100.01, 5));  // wrong side's band
    CHECK(!book.add_order(id++, Side::Sell, 99.99, 5));
    CHECK(book.get_bbo() == b);
    CHECK(same(book.get_bids(3), std::vector<Level>(bids.begin(), bids.begin() + 3)));
}

// ---------------------------------------------------------------------------
// Whole-number prices go through the price API. While the tick overload was
// also named add_order, add_order(id, Buy, 75, 10) meant 75 ticks (0.75) and
// was rejected, and a long price did not compile (ambiguous overload).
// ---------------------------------------------------------------------------
template <class Book>
static void test_whole_number_prices() {
    Book book;
    const long ask_price = 101;
    const unsigned far_ask = 120u;
    CHECK(book.add_order(1, Side::Buy, 75, 10));         // int
    CHECK(book.add_order(2, Side::Sell, ask_price, 4));  // long
    CHECK(book.add_order(3, Side::Sell, far_ask, 1));    // unsigned
    const BBO b = book.get_bbo();
    CHECK(b.has_bid && b.bid_price == 75.0 && b.bid_qty == 10);
    CHECK(b.has_ask && b.ask_price == 101.0 && b.ask_qty == 4);
    CHECK(book.get_asks(5).size() == 2);
}

// ---------------------------------------------------------------------------
// Emptying a full side in random order: the best level keeps moving, which in
// C means scanning the occupancy bitmap across word boundaries.
// ---------------------------------------------------------------------------
template <class Book>
static void test_drain_full_book(uint32_t seed) {
    Book book;
    ReferenceBook ref;
    std::vector<uint64_t> ids;
    uint64_t id = 1000;
    for (int t = kBidMinTick; t <= kAskMaxTick; ++t) {
        if (t == 10000) continue;
        const Side side = t <= kBidMaxTick ? Side::Buy : Side::Sell;
        CHECK(book.add_order_ticks(id, side, t, 1 + t % 7) == ref.add_order_ticks(id, side, t, 1 + t % 7));
        ids.push_back(id++);
    }
    std::mt19937 rng(seed);
    std::shuffle(ids.begin(), ids.end(), rng);
    bool ok = true;
    for (uint64_t x : ids) {
        ok = ok && book.cancel_order(x) && ref.cancel_order(x) && book.get_bbo() == ref.get_bbo();
    }
    CHECK(ok);
    const BBO e = book.get_bbo();
    CHECK(!e.has_bid && !e.has_ask);
    CHECK(book.get_bids(10).empty() && book.get_asks(10).empty());
}

// ---------------------------------------------------------------------------
// Random differential test with hostile input mixed in
// ---------------------------------------------------------------------------
template <class Book>
static void test_random_vs_reference(uint32_t seed, int n_ops) {
    std::mt19937_64 rng(seed);
    Book book;
    ReferenceBook ref;
    std::vector<uint64_t> live, dead;
    size_t mismatches = 0;
    auto pick = [&](size_t n) { return static_cast<size_t>(rng() % n); };

    for (int i = 0; i < n_ops; ++i) {
        const unsigned r = static_cast<unsigned>(rng() % 100);
        bool got = false, want = false;
        if (r < 55 || live.empty()) {
            // new order with a random 64-bit id; the extremes show up too
            uint64_t id = rng();
            if (i == 10) id = 0;
            if (i == 20) id = ~0ull;
            const Side side = (rng() & 1) ? Side::Buy : Side::Sell;
            // ticks cluster near the touch so levels fill, empty and refill
            const int off = static_cast<int>(rng() % 40);
            const int tick = side == Side::Buy ? kBidMaxTick - off : kAskMinTick + off;
            const long qty = 1 + static_cast<long>(rng() % 100);
            got = book.add_order_ticks(id, side, tick, qty);
            want = ref.add_order_ticks(id, side, tick, qty);
            if (want) live.push_back(id);
        } else if (r < 85) {
            // cancel a live order
            const size_t k = pick(live.size());
            const uint64_t id = live[k];
            live[k] = live.back();
            live.pop_back();
            got = book.cancel_order(id);
            want = ref.cancel_order(id);
            dead.push_back(id);
        } else if (r < 90 && !dead.empty()) {
            // cancel an already-cancelled id (must be false), or re-add it
            const uint64_t id = dead[pick(dead.size())];
            if (rng() & 1) {
                got = book.cancel_order(id);
                want = ref.cancel_order(id);
                if (want) live.erase(std::find(live.begin(), live.end(), id));  // it had been re-added
            } else {
                got = book.add_order_ticks(id, Side::Buy, kBidMaxTick, 1);
                want = ref.add_order_ticks(id, Side::Buy, kBidMaxTick, 1);
                if (want) live.push_back(id);
            }
        } else if (r < 94) {
            // duplicate add of a live id (must be rejected)
            const uint64_t id = live[pick(live.size())];
            got = book.add_order_ticks(id, Side::Sell, kAskMinTick, 5);
            want = ref.add_order_ticks(id, Side::Sell, kAskMinTick, 5);
        } else if (r < 97) {
            // invalid price or quantity (must be rejected)
            const int bad_ticks[] = {kBidMinTick - 1, 10000, kAskMaxTick + 1, kAskMinTick, -5};
            const long bad_qty[] = {0, -1, 2147483648L};
            const uint64_t id = rng();
            if (rng() & 1) {
                const int t = bad_ticks[pick(5)];
                got = book.add_order_ticks(id, Side::Buy, t, 10);
                want = ref.add_order_ticks(id, Side::Buy, t, 10);
            } else {
                const long q = bad_qty[pick(3)];
                got = book.add_order_ticks(id, Side::Sell, kAskMinTick + 3, q);
                want = ref.add_order_ticks(id, Side::Sell, kAskMinTick + 3, q);
            }
        } else {
            // cancel an id that never existed
            const uint64_t id = rng() | 1ull << 63;
            got = book.cancel_order(id);
            want = ref.cancel_order(id);
        }
        bool ok = got == want && book.get_bbo() == ref.get_bbo() && same(book.get_bids(10), ref.get_bids(10)) &&
                  same(book.get_asks(10), ref.get_asks(10));
        if (i % 1000 == 0) {
            ok = ok && same(book.get_bids(kLevelsPerSide), ref.get_bids(kLevelsPerSide)) &&
                 same(book.get_asks(0), ref.get_asks(0)) && same(book.get_bids(-1), ref.get_bids(-1));
        }
        mismatches += !ok;
    }
    CHECK(mismatches == 0);
    if (mismatches) std::printf("  %zu mismatching ops (seed %u)\n", mismatches, seed);
    // drain: every remaining live order cancels exactly once
    bool drained = true;
    for (uint64_t id : live) drained = drained && book.cancel_order(id) && !book.cancel_order(id);
    CHECK(drained);
    const BBO e = book.get_bbo();
    CHECK(!e.has_bid && !e.has_ask);
}

template <class Book>
static void run_all(const char* name) {
    const int before = g_failures;
    test_double_prices<Book>();
    test_whole_number_prices<Book>();
    test_drain_full_book<Book>(1);
    for (uint32_t seed = 1; seed <= 3; ++seed) test_random_vs_reference<Book>(seed, 200'000);
    std::printf("%-26s %s\n", name, g_failures == before ? "PASS" : "FAIL");
}

int main() {
    std::printf("Order-book regression and differential tests\n");
    run_all<OrderBookA>("Implementation A");
    run_all<OrderBookB>("Implementation B");
    run_all<OrderBookC>("Implementation C");
    run_all<SmallTableC>("Implementation C (16-slot)");
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
