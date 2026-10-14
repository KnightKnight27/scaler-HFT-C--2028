#include "order_books.hpp"

#include <iostream>
#include <stdexcept>

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <class Book>
void run_api_tests() {
    Book book;
    BBO bbo = book.get_bbo();
    check(!bbo.has_bid && !bbo.has_ask, "empty BBO");
    check(!book.cancel_order(999), "unknown cancel");
    check(book.get_bids(10).empty(), "empty bids");
    check(book.get_asks(10).empty(), "empty asks");

    book.add_order(1, Side::Buy, 99.99, 10);
    book.add_order(2, Side::Buy, 99.99, 15);
    book.add_order(3, Side::Buy, 98.50, 20);
    book.add_order(4, Side::Sell, 100.01, 7);
    book.add_order(5, Side::Sell, 101.25, 9);

    bbo = book.get_bbo();
    check(bbo.has_bid && bbo.bid_price == 99.99 && bbo.bid_qty == 25, "bid BBO");
    check(bbo.has_ask && bbo.ask_price == 100.01 && bbo.ask_qty == 7, "ask BBO");
    const auto bids = book.get_bids(10);
    check(bids.size() == 2, "insufficient depth");
    check(bids[0].price == 99.99 && bids[0].qty == 25, "aggregated level");
    check(bids[1].price == 98.50 && bids[1].qty == 20, "level ordering");

    check(book.cancel_order(1), "known cancel");
    check(!book.cancel_order(1), "repeated cancel");
    check(book.get_bbo().bid_qty == 15, "partial level cancel");
    check(book.cancel_order(2), "last order at best level");
    check(book.get_bbo().bid_price == 98.50, "best price repair");

    bool threw = false;
    try { book.add_order(6, Side::Buy, 100.00, 1); }
    catch (const std::invalid_argument&) { threw = true; }
    check(threw, "bid range validation");
    threw = false;
    try { book.add_order(7, Side::Sell, 100.011, 1); }
    catch (const std::invalid_argument&) { threw = true; }
    check(threw, "tick validation");
    threw = false;
    try { book.add_order(8, Side::Sell, 110.00, 0); }
    catch (const std::invalid_argument&) { threw = true; }
    check(threw, "quantity validation");
    threw = false;
    try { book.add_order(4, Side::Sell, 110.00, 1); }
    catch (const std::invalid_argument&) { threw = true; }
    check(threw, "duplicate ID validation");
}

int main() {
    run_api_tests<MapOrderBook>();
    run_api_tests<IndexedOrderBook>();
    run_api_tests<ArrayOrderBook>();
    std::cout << "All API edge-case tests passed.\n";
}
