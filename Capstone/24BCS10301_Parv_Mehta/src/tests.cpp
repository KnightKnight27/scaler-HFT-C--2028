// Unit tests. Every test runs against A, B and C. Build and run: make test

#include <cstdio>
#include <string>

#include "array_book.h"
#include "heap_book.h"
#include "map_book.h"
#include "reference_book.h"
#include "workload.h"

static int g_failed = 0;
static int g_checked = 0;

#define CHECK(cond)                                                             \
    do {                                                                        \
        ++g_checked;                                                            \
        if (!(cond)) {                                                          \
            ++g_failed;                                                         \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                       \
    } while (0)

static bool bbo_is(const BBO& b, bool has_bid, double bp, long bq, bool has_ask, double ap, long aq) {
    if (b.has_bid != has_bid || b.has_ask != has_ask) return false;
    if (has_bid && (to_tick(b.bid_price) != to_tick(bp) || b.bid_qty != bq)) return false;
    if (has_ask && (to_tick(b.ask_price) != to_tick(ap) || b.ask_qty != aq)) return false;
    return true;
}

template <class Book>
static void test_empty_book() {
    Book book;
    BBO b = book.get_bbo();
    CHECK(!b.has_bid && !b.has_ask);
    CHECK(book.get_bids(10).empty());
    CHECK(book.get_asks(10).empty());
    CHECK(book.cancel_order(1) == false);
    CHECK(book.cancel_order(0) == false);
}

template <class Book>
static void test_single_order() {
    Book book;
    book.add_order(1, Side::Buy, 99.50, 10);
    CHECK(bbo_is(book.get_bbo(), true, 99.50, 10, false, 0, 0));
    CHECK(book.cancel_order(2) == false);      // unknown id
    CHECK(book.cancel_order(1) == true);
    CHECK(book.cancel_order(1) == false);      // already cancelled
    BBO b = book.get_bbo();
    CHECK(!b.has_bid && !b.has_ask);
    CHECK(book.order_count() == 0);
}

template <class Book>
static void test_same_price_aggregation() {
    Book book;
    book.add_order(1, Side::Buy, 99.50, 10);
    book.add_order(2, Side::Buy, 99.50, 20);
    book.add_order(3, Side::Buy, 99.50, 30);
    book.add_order(4, Side::Sell, 100.25, 7);
    book.add_order(5, Side::Sell, 100.25, 8);
    CHECK(bbo_is(book.get_bbo(), true, 99.50, 60, true, 100.25, 15));
    CHECK(book.get_bids(10).size() == 1);
    CHECK(book.cancel_order(2));
    CHECK(bbo_is(book.get_bbo(), true, 99.50, 40, true, 100.25, 15));
    CHECK(book.cancel_order(1));
    CHECK(book.cancel_order(3));
    CHECK(bbo_is(book.get_bbo(), false, 0, 0, true, 100.25, 15));
    CHECK(book.cancel_order(4));
    CHECK(book.cancel_order(5));
    CHECK(!book.get_bbo().has_ask);
}

// Best price must move correctly when the best level empties. 50.63/50.64 sit
// on a 64-bit word boundary in implementation C's bitmap, 90.00 and 99.99 are
// in different summary words.
template <class Book>
static void test_best_moves_on_cancel() {
    Book book;
    book.add_order(10, Side::Buy, 50.00, 1);
    book.add_order(11, Side::Buy, 50.63, 1);
    book.add_order(12, Side::Buy, 50.64, 1);
    book.add_order(13, Side::Buy, 90.00, 1);
    book.add_order(14, Side::Buy, 99.99, 1);
    book.add_order(20, Side::Sell, 150.00, 1);
    book.add_order(21, Side::Sell, 149.37, 1);
    book.add_order(22, Side::Sell, 149.36, 1);
    book.add_order(23, Side::Sell, 110.00, 1);
    book.add_order(24, Side::Sell, 100.01, 1);

    CHECK(bbo_is(book.get_bbo(), true, 99.99, 1, true, 100.01, 1));
    CHECK(book.cancel_order(14) && book.cancel_order(24));
    CHECK(bbo_is(book.get_bbo(), true, 90.00, 1, true, 110.00, 1));
    CHECK(book.cancel_order(13) && book.cancel_order(23));
    CHECK(bbo_is(book.get_bbo(), true, 50.64, 1, true, 149.36, 1));
    CHECK(book.cancel_order(12) && book.cancel_order(22));
    CHECK(bbo_is(book.get_bbo(), true, 50.63, 1, true, 149.37, 1));
    CHECK(book.cancel_order(11) && book.cancel_order(21));
    CHECK(bbo_is(book.get_bbo(), true, 50.00, 1, true, 150.00, 1));
    CHECK(book.cancel_order(10) && book.cancel_order(20));
    CHECK(bbo_is(book.get_bbo(), false, 0, 0, false, 0, 0));

    // best comes back after the side was emptied
    book.add_order(30, Side::Buy, 75.25, 5);
    CHECK(bbo_is(book.get_bbo(), true, 75.25, 5, false, 0, 0));
}

template <class Book>
static void test_depth() {
    Book book;
    book.add_order(1, Side::Buy, 99.99, 1);
    book.add_order(2, Side::Buy, 99.98, 2);
    book.add_order(3, Side::Buy, 99.98, 3);
    book.add_order(4, Side::Buy, 99.50, 4);
    book.add_order(5, Side::Sell, 100.01, 5);
    book.add_order(6, Side::Sell, 100.10, 6);
    book.add_order(7, Side::Sell, 100.05, 7);

    // ask for 10 levels, only 3 exist
    std::vector<Level> bids = book.get_bids(10);
    CHECK(bids.size() == 3);
    if (bids.size() == 3) {
        CHECK(to_tick(bids[0].price) == 9999 && bids[0].qty == 1);
        CHECK(to_tick(bids[1].price) == 9998 && bids[1].qty == 5);
        CHECK(to_tick(bids[2].price) == 9950 && bids[2].qty == 4);
    }
    std::vector<Level> asks = book.get_asks(10);
    CHECK(asks.size() == 3);
    if (asks.size() == 3) {
        CHECK(to_tick(asks[0].price) == 10001 && asks[0].qty == 5);
        CHECK(to_tick(asks[1].price) == 10005 && asks[1].qty == 7);
        CHECK(to_tick(asks[2].price) == 10010 && asks[2].qty == 6);
    }
    // smaller depth than available
    CHECK(book.get_bids(2).size() == 2);
    CHECK(book.get_asks(1).size() == 1);
    CHECK(book.get_bids(0).empty());
}

template <class Book>
static void test_invalid_input_is_ignored() {
    Book book;
    book.add_order(1, Side::Buy, 99.00, 0);      // zero qty
    book.add_order(2, Side::Buy, 99.00, -5);     // negative qty
    book.add_order(3, Side::Buy, 49.99, 1);      // below bid band
    book.add_order(4, Side::Buy, 100.00, 1);     // mid price is not a valid level
    book.add_order(5, Side::Sell, 100.00, 1);
    book.add_order(6, Side::Sell, 150.01, 1);    // above ask band
    BBO b = book.get_bbo();
    CHECK(!b.has_bid && !b.has_ask);
    CHECK(book.order_count() == 0);

    book.add_order(7, Side::Buy, 60.00, 10);
    book.add_order(7, Side::Buy, 61.00, 10);     // duplicate id, must be ignored
    CHECK(bbo_is(book.get_bbo(), true, 60.00, 10, false, 0, 0));
    CHECK(book.order_count() == 1);
}

// Random workload against the brute force reference. A narrow band makes
// levels appear and disappear constantly, which stresses best-price tracking.
template <class Book>
static void test_random_vs_reference(uint32_t seed, size_t n, int band) {
    WorkloadConfig cfg;
    cfg.seed = seed;
    cfg.ops = n;
    cfg.band = band;
    std::vector<Op> ops = generate_workload(cfg);

    Book book;
    ReferenceBook ref;
    int mismatches = 0;
    for (size_t i = 0; i < ops.size() && mismatches < 5; ++i) {
        const Op& op = ops[i];
        if (op.type == OpType::Add) {
            book.add_order(op.id, op.side, to_price(op.tick), op.qty);
            ref.add_order(op.id, op.side, to_price(op.tick), op.qty);
        } else if (op.type == OpType::Cancel) {
            bool a = book.cancel_order(op.id);
            bool b = ref.cancel_order(op.id);
            if (a != b) { ++mismatches; std::printf("  cancel result differs at op %zu\n", i); }
        }
        if (i % 7 == 0 && !same_bbo(book.get_bbo(), ref.get_bbo())) {
            ++mismatches;
            std::printf("  bbo differs at op %zu\n", i);
        }
        if (i % 97 == 0) {
            if (!same_levels(book.get_bids(10), ref.get_bids(10))) { ++mismatches; std::printf("  bids differ at op %zu\n", i); }
            if (!same_levels(book.get_asks(10), ref.get_asks(10))) { ++mismatches; std::printf("  asks differ at op %zu\n", i); }
        }
    }
    CHECK(same_levels(book.get_bids(kLevelsPerSide), ref.get_bids(kLevelsPerSide)));
    CHECK(same_levels(book.get_asks(kLevelsPerSide), ref.get_asks(kLevelsPerSide)));
    CHECK(book.order_count() == ref.order_count());
    CHECK(mismatches == 0);
}

// C only: start with a tiny table so it has to grow, and cancel in an order
// that exercises backward-shift deletion.
static void test_array_book_table_growth() {
    ArrayBook book(4);
    const int n = 20000;
    for (int i = 1; i <= n; ++i) book.add_order((uint64_t)i * 7919, Side::Buy, 99.00, 1);
    CHECK(book.order_count() == (size_t)n);
    CHECK(book.get_bbo().bid_qty == n);
    for (int i = n; i >= 1; i -= 2) CHECK(book.cancel_order((uint64_t)i * 7919));
    for (int i = 1; i <= n; i += 2) CHECK(book.cancel_order((uint64_t)i * 7919));
    CHECK(book.order_count() == 0);
    CHECK(!book.get_bbo().has_bid);
    for (int i = 1; i <= n; ++i) CHECK(book.cancel_order((uint64_t)i * 7919) == false);
}

template <class Book>
static void run_all(const char* label) {
    const int before = g_failed;
    std::printf("Implementation %s\n", label);
    test_empty_book<Book>();
    test_single_order<Book>();
    test_same_price_aggregation<Book>();
    test_best_moves_on_cancel<Book>();
    test_depth<Book>();
    test_invalid_input_is_ignored<Book>();
    test_random_vs_reference<Book>(7, 50000, 20);
    test_random_vs_reference<Book>(8, 50000, 3);
    test_random_vs_reference<Book>(9, 20000, kLevelsPerSide);
    std::printf("  %s\n", g_failed == before ? "all checks passed" : "FAILED");
}

int main() {
    run_all<MapBook>("A (MapBook)");
    run_all<HeapBook>("B (HeapBook)");
    run_all<ArrayBook>("C (ArrayBook)");
    std::printf("Implementation C hash table growth\n");
    const int before = g_failed;
    test_array_book_table_growth();
    std::printf("  %s\n", g_failed == before ? "all checks passed" : "FAILED");

    std::printf("\n%d checks, %d failed\n", g_checked, g_failed);
    return g_failed == 0 ? 0 : 1;
}
