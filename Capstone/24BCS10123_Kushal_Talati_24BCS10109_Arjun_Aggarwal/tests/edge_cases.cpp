// hand written edge cases, run on all three books
#include "../order_book_map.hpp"
#include "../order_book_heap.hpp"
#include "../order_book_array.hpp"
#include <cstdio>

static int fails = 0, checks = 0;

#define CHECK(cond) do { ++checks; if (!(cond)) { ++fails; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static bool lvl_is(const Level& l, double p, long q) { return to_tick(l.price) == to_tick(p) && l.qty == q; }

template <class Book>
static void edge_suite() {
    {   // empty book
        Book b;
        BBO x = b.get_bbo();
        CHECK(!x.has_bid && !x.has_ask);
        CHECK(b.get_bids(10).empty());
        CHECK(b.get_asks(10).empty());
        CHECK(!b.cancel_order(1));  // nothing there
        CHECK(!b.cancel_order(0));
    }
    {   // cancel unknown + double cancel
        Book b;
        CHECK(b.add_order(1, Side::Buy, 60.00, 10));
        CHECK(!b.cancel_order(99));
        CHECK(b.cancel_order(1));
        CHECK(!b.cancel_order(1));
        CHECK(!b.get_bbo().has_bid);
    }
    {   // 3 levels, depth 10 -> 3, sorted right way
        Book b;
        b.add_order(1, Side::Buy, 99.00, 5);
        b.add_order(2, Side::Buy, 98.00, 6);
        b.add_order(3, Side::Buy, 97.00, 7);
        b.add_order(4, Side::Sell, 101.00, 1);
        b.add_order(5, Side::Sell, 103.00, 2);
        b.add_order(6, Side::Sell, 102.00, 3);
        auto bids = b.get_bids(10);
        auto asks = b.get_asks(10);
        CHECK(bids.size() == 3);
        CHECK(asks.size() == 3);
        CHECK(lvl_is(bids[0], 99.00, 5) && lvl_is(bids[1], 98.00, 6) && lvl_is(bids[2], 97.00, 7));
        CHECK(lvl_is(asks[0], 101.00, 1) && lvl_is(asks[1], 102.00, 3) && lvl_is(asks[2], 103.00, 2));
        CHECK(b.get_bids(2).size() == 2);
        CHECK(b.get_bids(0).empty());  // depth 0
        CHECK(b.get_asks(0).empty());
        CHECK(b.get_bids(-3).empty());  // negative depth, treat as 0
        CHECK(b.get_bids().size() == 3);  // default depth 10
    }
    {   // cancel the best level, best moves down
        Book b;
        b.add_order(1, Side::Buy, 99.00, 5);
        b.add_order(2, Side::Buy, 98.00, 6);
        b.add_order(3, Side::Sell, 101.00, 1);
        b.add_order(4, Side::Sell, 102.00, 3);
        CHECK(b.cancel_order(1));
        BBO x = b.get_bbo();
        CHECK(x.has_bid && to_tick(x.bid_price) == 9800 && x.bid_qty == 6);
        CHECK(b.cancel_order(3));
        x = b.get_bbo();
        CHECK(x.has_ask && to_tick(x.ask_price) == 10200 && x.ask_qty == 3);
        CHECK(b.get_bids(10).size() == 1);
        CHECK(b.cancel_order(2) && b.cancel_order(4));
        x = b.get_bbo();
        CHECK(!x.has_bid && !x.has_ask);
    }
    {   // add to existing level, level qty is the sum, partial cancel keeps the level
        Book b;
        b.add_order(1, Side::Buy, 99.50, 10);
        b.add_order(2, Side::Buy, 99.50, 20);
        b.add_order(3, Side::Buy, 99.50, 30);
        BBO x = b.get_bbo();
        CHECK(x.has_bid && x.bid_qty == 60);
        CHECK(b.get_bids(10).size() == 1);
        CHECK(b.cancel_order(2));
        x = b.get_bbo();
        CHECK(x.has_bid && to_tick(x.bid_price) == 9950 && x.bid_qty == 40);
        CHECK(b.cancel_order(1) && b.cancel_order(3));
        CHECK(!b.get_bbo().has_bid);
        CHECK(b.get_bids(10).empty());
    }
    {   // cancel a middle level (not best), depth shrinks, best unchanged
        Book b;
        b.add_order(1, Side::Sell, 110.00, 1);
        b.add_order(2, Side::Sell, 120.00, 2);
        b.add_order(3, Side::Sell, 130.00, 3);
        CHECK(b.cancel_order(2));
        auto asks = b.get_asks(10);
        CHECK(asks.size() == 2 && lvl_is(asks[0], 110.00, 1) && lvl_is(asks[1], 130.00, 3));
        CHECK(to_tick(b.get_bbo().ask_price) == 11000);
    }
    {   // band boundaries
        Book b;
        CHECK(b.add_order(1, Side::Buy, 50.00, 1));
        CHECK(b.add_order(2, Side::Buy, 99.99, 1));
        CHECK(b.add_order(3, Side::Sell, 100.01, 1));
        CHECK(b.add_order(4, Side::Sell, 150.00, 1));
        CHECK(!b.add_order(5, Side::Buy, 49.99, 1));
        CHECK(!b.add_order(6, Side::Buy, 100.00, 1));
        CHECK(!b.add_order(7, Side::Buy, 100.01, 1));   // ask band price on the bid side
        CHECK(!b.add_order(8, Side::Sell, 100.00, 1));
        CHECK(!b.add_order(9, Side::Sell, 150.01, 1));
        CHECK(!b.add_order(10, Side::Sell, 99.99, 1));
        CHECK(!b.add_order(11, Side::Buy, 60.00, 0));   // qty 0
        CHECK(!b.add_order(12, Side::Buy, 60.00, -5));
        CHECK(!b.add_order(1, Side::Buy, 60.00, 5));    // id reuse
        BBO x = b.get_bbo();
        CHECK(to_tick(x.bid_price) == 9999 && to_tick(x.ask_price) == 10001);
        auto bids = b.get_bids(10), asks = b.get_asks(10);
        CHECK(bids.size() == 2 && lvl_is(bids[1], 50.00, 1));
        CHECK(asks.size() == 2 && lvl_is(asks[1], 150.00, 1));
        CHECK(!b.cancel_order(5));  // rejected add never existed
        CHECK(b.cancel_order(1) && b.cancel_order(2) && b.cancel_order(3) && b.cancel_order(4));
        x = b.get_bbo();
        CHECK(!x.has_bid && !x.has_ask);
    }
    {   // empty a level, re-add it, empty it again (this is the lazy deletion path in B)
        Book b;
        b.add_order(1, Side::Buy, 99.99, 1);
        b.add_order(2, Side::Buy, 99.98, 1);
        CHECK(b.cancel_order(1));
        CHECK(to_tick(b.get_bbo().bid_price) == 9998);
        b.add_order(3, Side::Buy, 99.99, 7);
        CHECK(to_tick(b.get_bbo().bid_price) == 9999 && b.get_bbo().bid_qty == 7);
        CHECK(b.cancel_order(3));
        CHECK(to_tick(b.get_bbo().bid_price) == 9998);
        b.add_order(4, Side::Buy, 99.99, 2);  // re-add without a bbo call in between
        CHECK(b.cancel_order(2));
        auto bids = b.get_bids(10);
        CHECK(bids.size() == 1 && lvl_is(bids[0], 99.99, 2));
        CHECK(b.cancel_order(4));
        CHECK(!b.get_bbo().has_bid && b.get_bids(10).empty());
    }
    {   // full depth with many levels
        Book b;
        uint64_t id = 1;
        for (int32_t t = BID_MIN_TICK; t <= BID_MAX_TICK; ++t) b.add_order(id++, Side::Buy, to_price(t), 1);
        for (int32_t t = ASK_MIN_TICK; t <= ASK_MAX_TICK; ++t) b.add_order(id++, Side::Sell, to_price(t), 1);
        auto bids = b.get_bids(LEVELS_PER_SIDE), asks = b.get_asks(LEVELS_PER_SIDE);
        CHECK(bids.size() == LEVELS_PER_SIDE && asks.size() == LEVELS_PER_SIDE);
        CHECK(lvl_is(bids[0], 99.99, 1) && lvl_is(bids.back(), 50.00, 1));
        CHECK(lvl_is(asks[0], 100.01, 1) && lvl_is(asks.back(), 150.00, 1));
        CHECK(b.get_bids(LEVELS_PER_SIDE + 100).size() == LEVELS_PER_SIDE);
    }
}

// only C has the slab / overflow split
static void array_overflow() {
    OrderBookArray b(4);  // slab holds ids 0..3, everything else goes to the hash map
    CHECK(b.add_order(2, Side::Buy, 70.00, 5));
    CHECK(b.add_order(1000, Side::Buy, 71.00, 6));
    CHECK(!b.add_order(1000, Side::Buy, 71.00, 6));
    CHECK(to_tick(b.get_bbo().bid_price) == 7100);
    CHECK(b.cancel_order(1000));
    CHECK(!b.cancel_order(1000));
    CHECK(to_tick(b.get_bbo().bid_price) == 7000);
    CHECK(b.cancel_order(2) && !b.get_bbo().has_bid);
}

int main() {
    const char* names[3] = {"A", "B", "C"};
    for (int i = 0; i < 3; ++i) {
        int before = fails, c0 = checks;
        if (i == 0) edge_suite<OrderBookMap>();
        if (i == 1) edge_suite<OrderBookHeap>();
        if (i == 2) { edge_suite<OrderBookArray>(); array_overflow(); }
        printf("Edge cases %s: %s (%d checks)\n", names[i], fails == before ? "PASS" : "FAIL", checks - c0);
    }
    return fails ? 1 : 0;
}
