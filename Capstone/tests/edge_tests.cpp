// edge_tests.cpp — hand-written edge cases from AGENT.md §4, run against each book.
//
//   edge_tests            run the oracle and every implemented book
//   edge_tests OABC       run any subset (O = oracle)
//
// Each test is a template on the book type, so every implementation runs the
// identical suite. CHECK records a failure and keeps going, so one run reports
// every broken case. The process exits non-zero if anything failed.
//
// Expected prices are written as literals (e.g. 99.99). tick_to_price(9999) computes
// 9999 / 100.0, and IEEE division is correctly rounded, so it yields the same double
// as the literal 99.99. Exact == comparison is therefore valid here.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

#include "book_a.h"
#include "book_b.h"
#include "book_c.h"
#include "oracle.h"

static_assert(implements_order_book<OracleBook>(), "the oracle must satisfy the order book interface");

namespace {

int g_failures = 0;
int g_checks = 0;
const char* g_current_test = "";

void report_failure(const char* file, int line, const std::string& what) {
    ++g_failures;
    std::printf("  FAIL [%s] %s:%d: %s\n", g_current_test, file, line, what.c_str());
}

#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) report_failure(__FILE__, __LINE__, #cond);           \
    } while (0)

std::string format_levels(const std::vector<Level>& levels) {
    std::string s = "[";
    for (std::size_t i = 0; i < levels.size(); ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s%.2f x %ld", i ? ", " : "", levels[i].price, levels[i].qty);
        s += buf;
    }
    return s + "]";
}

void check_levels(const char* file, int line, const std::vector<Level>& actual,
                  const std::vector<Level>& expected) {
    ++g_checks;
    bool same = actual.size() == expected.size();
    for (std::size_t i = 0; same && i < actual.size(); ++i) {
        same = actual[i].price == expected[i].price && actual[i].qty == expected[i].qty;
    }
    if (!same) {
        report_failure(file, line, "levels: expected " + format_levels(expected) +
                                       ", got " + format_levels(actual));
    }
}
#define CHECK_LEVELS(actual, ...) check_levels(__FILE__, __LINE__, (actual), std::vector<Level>__VA_ARGS__)

// --- tests -----------------------------------------------------------------

template <typename Book>
void test_empty_book() {
    g_current_test = "empty_book";
    const Book book;
    const BBO bbo = book.get_bbo();
    CHECK(!bbo.has_bid);
    CHECK(!bbo.has_ask);
    CHECK(book.get_bids(10).empty());
    CHECK(book.get_asks(10).empty());
    CHECK(book.check_invariants());
}

template <typename Book>
void test_fewer_levels_than_depth() {
    g_current_test = "fewer_levels_than_depth";
    Book book;
    book.add_order(1, Side::Buy, 95.00, 10);
    book.add_order(2, Side::Buy, 97.50, 20);
    book.add_order(3, Side::Buy, 96.25, 30);
    book.add_order(4, Side::Sell, 101.00, 5);
    book.add_order(5, Side::Sell, 100.50, 6);
    book.add_order(6, Side::Sell, 120.00, 7);

    CHECK_LEVELS(book.get_bids(10), {{97.50, 20}, {96.25, 30}, {95.00, 10}});  // descending
    CHECK_LEVELS(book.get_asks(10), {{100.50, 6}, {101.00, 5}, {120.00, 7}});  // ascending
    CHECK_LEVELS(book.get_bids(3), {{97.50, 20}, {96.25, 30}, {95.00, 10}});   // depth == levels
    CHECK(book.check_invariants());
}

template <typename Book>
void test_depth_zero_and_one() {
    g_current_test = "depth_zero_and_one";
    Book book;
    book.add_order(1, Side::Buy, 98.00, 10);
    book.add_order(2, Side::Buy, 99.00, 20);
    book.add_order(3, Side::Sell, 102.00, 30);
    book.add_order(4, Side::Sell, 101.00, 40);

    CHECK(book.get_bids(0).empty());
    CHECK(book.get_asks(0).empty());
    CHECK_LEVELS(book.get_bids(1), {{99.00, 20}});
    CHECK_LEVELS(book.get_asks(1), {{101.00, 40}});
}

template <typename Book>
void test_default_depth_is_ten() {
    g_current_test = "default_depth_is_ten";
    Book book;
    for (int i = 0; i < 15; ++i) {
        book.add_order(static_cast<uint64_t>(i), Side::Buy, 90.00 + i * 0.25, 1);
        book.add_order(static_cast<uint64_t>(100 + i), Side::Sell, 110.00 + i * 0.25, 1);
    }
    const std::vector<Level> bids = book.get_bids();
    const std::vector<Level> asks = book.get_asks();
    CHECK(bids.size() == 10);
    CHECK(asks.size() == 10);
    CHECK(!bids.empty() && bids.front().price == 93.50);  // 90.00 + 14 * 0.25
    CHECK(!asks.empty() && asks.front().price == 110.00);
}

template <typename Book>
void test_cancel_unknown_and_twice() {
    g_current_test = "cancel_unknown_and_twice";
    Book book;
    CHECK(!book.cancel_order(12345));  // empty book

    book.add_order(7, Side::Buy, 99.00, 10);
    CHECK(!book.cancel_order(8));      // unknown id
    CHECK(book.cancel_order(7));       // first cancel succeeds
    CHECK(!book.cancel_order(7));      // second cancel fails
    CHECK(!book.get_bbo().has_bid);
    CHECK(book.check_invariants());
}

template <typename Book>
void test_same_price_aggregation() {
    g_current_test = "same_price_aggregation";
    Book book;
    book.add_order(1, Side::Sell, 105.00, 10);
    book.add_order(2, Side::Sell, 105.00, 20);
    book.add_order(3, Side::Sell, 105.00, 30);
    CHECK_LEVELS(book.get_asks(10), {{105.00, 60}});

    CHECK(book.cancel_order(2));  // middle order
    CHECK_LEVELS(book.get_asks(10), {{105.00, 40}});  // level persists, qty drops

    CHECK(book.cancel_order(1));
    CHECK_LEVELS(book.get_asks(10), {{105.00, 30}});

    CHECK(book.cancel_order(3));
    CHECK(book.get_asks(10).empty());
    CHECK(book.check_invariants());
}

template <typename Book>
void test_cancel_best_moves_bbo() {
    g_current_test = "cancel_best_moves_bbo";
    Book book;
    book.add_order(1, Side::Buy, 99.00, 10);
    book.add_order(2, Side::Buy, 98.00, 20);
    book.add_order(3, Side::Buy, 99.00, 5);
    book.add_order(4, Side::Sell, 101.00, 15);
    book.add_order(5, Side::Sell, 103.00, 25);

    CHECK(book.cancel_order(1));  // best bid level still has order 3
    BBO bbo = book.get_bbo();
    CHECK(bbo.has_bid && bbo.bid_price == 99.00 && bbo.bid_qty == 5);

    CHECK(book.cancel_order(3));  // last order at best bid
    bbo = book.get_bbo();
    CHECK(bbo.has_bid && bbo.bid_price == 98.00 && bbo.bid_qty == 20);

    CHECK(book.cancel_order(4));  // last order at best ask
    bbo = book.get_bbo();
    CHECK(bbo.has_ask && bbo.ask_price == 103.00 && bbo.ask_qty == 25);
    CHECK(book.check_invariants());
}

template <typename Book>
void test_cancel_non_best_keeps_bbo() {
    g_current_test = "cancel_non_best_keeps_bbo";
    Book book;
    book.add_order(1, Side::Buy, 99.00, 10);
    book.add_order(2, Side::Buy, 97.00, 20);
    book.add_order(3, Side::Buy, 95.00, 30);

    CHECK(book.cancel_order(2));  // middle level disappears
    const BBO bbo = book.get_bbo();
    CHECK(bbo.has_bid && bbo.bid_price == 99.00 && bbo.bid_qty == 10);
    CHECK_LEVELS(book.get_bids(10), {{99.00, 10}, {95.00, 30}});
}

template <typename Book>
void test_cancel_last_on_side() {
    g_current_test = "cancel_last_on_side";
    Book book;
    book.add_order(1, Side::Buy, 99.00, 10);
    book.add_order(2, Side::Sell, 101.00, 20);

    CHECK(book.cancel_order(1));
    BBO bbo = book.get_bbo();
    CHECK(!bbo.has_bid);
    CHECK(bbo.has_ask && bbo.ask_price == 101.00 && bbo.ask_qty == 20);  // other side untouched

    CHECK(book.cancel_order(2));
    bbo = book.get_bbo();
    CHECK(!bbo.has_bid && !bbo.has_ask);
    CHECK(book.get_bids(10).empty() && book.get_asks(10).empty());
    CHECK(book.check_invariants());
}

template <typename Book>
void test_boundary_prices() {
    g_current_test = "boundary_prices";
    Book book;
    book.add_order(1, Side::Buy, 50.00, 1);
    book.add_order(2, Side::Buy, 99.99, 2);
    book.add_order(3, Side::Sell, 100.01, 3);
    book.add_order(4, Side::Sell, 150.00, 4);

    const BBO bbo = book.get_bbo();
    CHECK(bbo.has_bid && bbo.bid_price == 99.99 && bbo.bid_qty == 2);
    CHECK(bbo.has_ask && bbo.ask_price == 100.01 && bbo.ask_qty == 3);
    CHECK_LEVELS(book.get_bids(10), {{99.99, 2}, {50.00, 1}});
    CHECK_LEVELS(book.get_asks(10), {{100.01, 3}, {150.00, 4}});
    CHECK(book.check_invariants());

    CHECK(book.cancel_order(2));  // best bid -> lowest possible bid
    CHECK(book.get_bbo().bid_price == 50.00);
    CHECK(book.cancel_order(3));  // best ask -> highest possible ask
    CHECK(book.get_bbo().ask_price == 150.00);
}

// Prices that are "the same" but not bit-identical doubles must land on one level.
template <typename Book>
void test_float_price_rounding() {
    g_current_test = "float_price_rounding";
    Book book;
    const double computed = 100.0 + 0.01;  // not bit-identical to the literal 100.01
    book.add_order(1, Side::Sell, 100.01, 10);
    book.add_order(2, Side::Sell, computed, 20);
    book.add_order(3, Side::Sell, 100.009999999, 30);  // just below: must round, not truncate
    CHECK_LEVELS(book.get_asks(10), {{100.01, 60}});

    book.add_order(4, Side::Buy, 0.1 * 3 * 333.3, 5);  // ~99.99 with accumulated error
    CHECK_LEVELS(book.get_bids(10), {{99.99, 5}});
    CHECK(book.check_invariants());
}

template <typename Book>
void test_large_non_sequential_ids() {
    g_current_test = "large_non_sequential_ids";
    Book book;
    const uint64_t max_id = std::numeric_limits<uint64_t>::max();
    const uint64_t ids[] = {max_id, 0, uint64_t{1} << 63, 0x9E3779B97F4A7C15ull, 42, max_id - 1};
    long qty = 1;
    for (const uint64_t id : ids) book.add_order(id, Side::Buy, 90.00, qty++);  // qty 1..6
    CHECK_LEVELS(book.get_bids(10), {{90.00, 21}});

    CHECK(book.cancel_order(max_id));          // qty 1
    CHECK(book.cancel_order(uint64_t{1} << 63));  // qty 3
    CHECK(!book.cancel_order(max_id));
    CHECK(!book.cancel_order(max_id - 2));     // never added
    CHECK_LEVELS(book.get_bids(10), {{90.00, 17}});
    CHECK(book.check_invariants());
}

template <typename Book>
void test_level_recreated_after_empty() {
    g_current_test = "level_recreated_after_empty";
    Book book;
    book.add_order(1, Side::Buy, 99.50, 40);
    CHECK(book.cancel_order(1));
    CHECK(!book.get_bbo().has_bid);

    book.add_order(2, Side::Buy, 99.50, 7);  // must not resurrect the old qty
    CHECK_LEVELS(book.get_bids(10), {{99.50, 7}});
    CHECK(book.check_invariants());
}

// Two sides never interfere: cancelling every bid leaves asks exactly as they were.
template <typename Book>
void test_sides_independent() {
    g_current_test = "sides_independent";
    Book book;
    for (int i = 0; i < 20; ++i) {
        book.add_order(static_cast<uint64_t>(i), Side::Buy, 99.99 - i * 0.01, i + 1);
        book.add_order(static_cast<uint64_t>(1000 + i), Side::Sell, 100.01 + i * 0.01, i + 1);
    }
    const std::vector<Level> asks_before = book.get_asks(20);
    for (int i = 0; i < 20; ++i) CHECK(book.cancel_order(static_cast<uint64_t>(i)));
    CHECK(!book.get_bbo().has_bid);
    check_levels(__FILE__, __LINE__, book.get_asks(20), asks_before);
    CHECK(book.check_invariants());
}

// After the best level empties, the next best can be far away. The ticks used here sit
// on C's bitmap word and summary-word boundaries (index = tick - 5000; a word is 64
// indices, a summary bit covers 4,096 indices), but the test applies to every book.
template <typename Book>
void test_far_jump_after_cancel_best() {
    g_current_test = "far_jump_after_cancel_best";
    Book book;
    book.add_order(1, Side::Buy, 50.00, 1);    // index 0
    book.add_order(2, Side::Buy, 50.63, 2);    // index 63, last bit of word 0
    book.add_order(3, Side::Buy, 50.64, 3);    // index 64, first bit of word 1
    book.add_order(4, Side::Buy, 90.96, 4);    // index 4096, first word of summary word 1
    book.add_order(5, Side::Sell, 100.01, 5);  // index 5001
    book.add_order(6, Side::Sell, 131.92, 6);  // index 8192, first word of summary word 2
    book.add_order(7, Side::Sell, 150.00, 7);  // index 10000, last valid index

    CHECK(book.cancel_order(4));  // 90.96 -> 50.64, crossing a summary-word boundary
    CHECK(book.get_bbo().bid_price == 50.64);
    CHECK(book.cancel_order(3));  // 50.64 -> 50.63, crossing a word boundary
    CHECK(book.get_bbo().bid_price == 50.63);
    CHECK(book.cancel_order(2));
    CHECK(book.get_bbo().bid_price == 50.00);
    CHECK_LEVELS(book.get_bids(10), {{50.00, 1}});

    CHECK(book.cancel_order(5));  // 100.01 -> 131.92, crossing a summary-word boundary
    CHECK(book.get_bbo().ask_price == 131.92);
    CHECK(book.cancel_order(6));
    CHECK(book.get_bbo().ask_price == 150.00);
    CHECK(book.cancel_order(7));
    CHECK(!book.get_bbo().has_ask);
    CHECK(book.check_invariants());
}

// C only: start from a 2-slot hash table, so it doubles repeatedly and probe chains get
// long, then compare against the oracle under random adds and cancels with random 64-bit
// ids. This exercises grow() and backward-shift deletion, which the default 2M-slot table
// never stresses in the graded workload.
void test_c_small_table_vs_oracle() {
    g_current_test = "c_small_table_vs_oracle";
    BookC book(2);
    OracleBook oracle;
    std::mt19937_64 rng(7);
    std::vector<uint64_t> live;

    for (int op = 0; op < 50'000; ++op) {
        const uint64_t roll = rng() % 100;
        if (roll < 55 || live.empty()) {
            const uint64_t id = rng();  // random 64-bit ids; a repeat is astronomically unlikely
            const bool buy = rng() % 2 == 0;
            const Tick tick = buy ? static_cast<Tick>(kMinBidTick + rng() % 5000)
                                  : static_cast<Tick>(kMinAskTick + rng() % 5000);
            const long qty = static_cast<long>(1 + rng() % 100);
            book.add_order(id, buy ? Side::Buy : Side::Sell, tick_to_price(tick), qty);
            oracle.add_order(id, buy ? Side::Buy : Side::Sell, tick_to_price(tick), qty);
            live.push_back(id);
        } else if (roll < 95) {
            const std::size_t index = static_cast<std::size_t>(rng() % live.size());
            const uint64_t id = live[index];
            live[index] = live.back();
            live.pop_back();
            CHECK(book.cancel_order(id) == oracle.cancel_order(id));
        } else {
            const uint64_t id = rng();  // almost surely unknown
            CHECK(book.cancel_order(id) == oracle.cancel_order(id));
        }

        if (op % 500 == 0) {
            check_levels(__FILE__, __LINE__, book.get_bids(kNumTicks), oracle.get_bids(kNumTicks));
            check_levels(__FILE__, __LINE__, book.get_asks(kNumTicks), oracle.get_asks(kNumTicks));
            CHECK(book.check_invariants());
            if (g_failures > 0) return;  // one diverged book produces a flood of failures
        }
    }
    CHECK(book.check_invariants());
}

template <typename Book>
void run_all(const char* name) {
    const int failures_before = g_failures;
    const int checks_before = g_checks;
    std::printf("%s:\n", name);

    test_empty_book<Book>();
    test_fewer_levels_than_depth<Book>();
    test_depth_zero_and_one<Book>();
    test_default_depth_is_ten<Book>();
    test_cancel_unknown_and_twice<Book>();
    test_same_price_aggregation<Book>();
    test_cancel_best_moves_bbo<Book>();
    test_cancel_non_best_keeps_bbo<Book>();
    test_cancel_last_on_side<Book>();
    test_boundary_prices<Book>();
    test_float_price_rounding<Book>();
    test_large_non_sequential_ids<Book>();
    test_level_recreated_after_empty<Book>();
    test_sides_independent<Book>();
    test_far_jump_after_cancel_best<Book>();
    if (std::is_same<Book, BookC>::value) test_c_small_table_vs_oracle();

    const int failed = g_failures - failures_before;
    std::printf("  %s (%d checks, %d failed)\n", failed == 0 ? "PASS" : "FAIL",
                g_checks - checks_before, failed);
}

}  // namespace

int main(int argc, char** argv) {
    // The oracle runs first: the verifier trusts it, so it must pass the same suite.
    const char* which = argc > 1 ? argv[1] : "OABC";
    if (std::strchr(which, 'O')) run_all<OracleBook>("Oracle");
    if (std::strchr(which, 'A')) run_all<BookA>("Implementation A");
    if (std::strchr(which, 'B')) run_all<BookB>("Implementation B");
    if (std::strchr(which, 'C')) run_all<BookC>("Implementation C");
    return g_failures == 0 ? 0 : 1;
}
