#include <cassert>
#include <iostream>

#include "order_book_third.hpp"

int main() {
    OrderBookC ob;
    BBO e = ob.get_bbo();
    assert(!e.has_bid && !e.has_ask);
    assert(!ob.cancel_order(999));

    ob.add_order(1, Side::Buy, 99.98, 100);
    ob.add_order(2, Side::Buy, 99.98, 50);
    ob.add_order(3, Side::Buy, 99.50, 10);
    ob.add_order(4, Side::Sell, 100.01, 20);
    ob.add_order(5, Side::Sell, 100.05, 30);

    BBO b = ob.get_bbo();
    assert(b.has_bid && b.bid_price == 99.98 && b.bid_qty == 150);
    assert(b.has_ask && b.ask_price == 100.01 && b.ask_qty == 20);

    assert(ob.cancel_order(1) && !ob.cancel_order(1));
    assert(ob.get_bbo().bid_qty == 50);
    assert(ob.cancel_order(2));
    assert(ob.get_bbo().bid_price == 99.50);

    assert(ob.cancel_order(4));
    assert(ob.get_bbo().ask_price == 100.05);

    assert(ob.get_bids(10).size() == 1);
    assert(ob.get_asks(10).size() == 1);
    assert(ob.get_bids(0).empty());
    assert(ob.get_bids(-1).empty());

    assert(ob.cancel_order(3) && ob.cancel_order(5));
    assert(!ob.get_bbo().has_bid && !ob.get_bbo().has_ask);

    OrderBookC test;

    test.add_order(100, Side::Buy, 99.98, 10);
    assert(test.cancel_order(100));

    test.add_order(100, Side::Buy, 99.99, 20);
    assert(!test.cancel_order(100));
    assert(!test.get_bbo().has_bid);

    test.add_order(101, Side::Buy, 99.981, 10);
    assert(!test.get_bbo().has_bid);

    test.add_order(102, Side::Buy, 100.01, 10);
    test.add_order(103, Side::Sell, 99.99, 10);
    test.add_order(104, Side::Buy, 99.99, 0);
    test.add_order(105, Side::Buy, 49.99, 10);
    test.add_order(106, Side::Sell, 150.01, 10);
    assert(!test.get_bbo().has_bid && !test.get_bbo().has_ask);

    test.add_order(107, Side::Buy, 50.00, 1);
    test.add_order(108, Side::Sell, 150.00, 1);
    assert(test.get_bbo().bid_price == 50.00 && test.get_bbo().ask_price == 150.00);

    std::cout << "Implementation C: PASS\n";
}
