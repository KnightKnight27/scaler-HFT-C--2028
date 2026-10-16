
#include "order_book.h"

#include <cmath>
#include <iostream>

int main() {
    auto book = make_heap_order_book();

    book->add_order(1, Side::Buy, 99.98, 10);
    book->add_order(2, Side::Buy, 99.98, 20);
    book->add_order(3, Side::Buy, 99.90, 15);

    book->add_order(4, Side::Sell, 100.01, 12);
    book->add_order(5, Side::Sell, 100.05, 8);

    BBO bbo = book->get_bbo();

    if (!bbo.has_bid || !bbo.has_ask ||
        std::abs(bbo.bid_price - 99.98) > 1e-9 ||
        bbo.bid_qty != 30 ||
        std::abs(bbo.ask_price - 100.01) > 1e-9 ||
        bbo.ask_qty != 12) {
        std::cerr << "FAIL: incorrect BBO\n";
        return 1;
    }

    if (!book->cancel_order(1)) {
        std::cerr << "FAIL: cancellation failed\n";
        return 1;
    }

    if (book->cancel_order(1)) {
        std::cerr << "FAIL: cancelled order was cancelled again\n";
        return 1;
    }

    bbo = book->get_bbo();

    if (bbo.bid_qty != 20) {
        std::cerr << "FAIL: incorrect quantity after cancellation\n";
        return 1;
    }

    auto bids = book->get_bids(10);
    auto asks = book->get_asks(10);

    if (bids.size() != 2 || asks.size() != 2) {
        std::cerr << "FAIL: incorrect depth\n";
        return 1;
    }

    if (bids[0].price < bids[1].price ||
        asks[0].price > asks[1].price) {
        std::cerr << "FAIL: levels are not sorted correctly\n";
        return 1;
    }

    std::cout << "Implementation B: PASS\n";
    return 0;
}
