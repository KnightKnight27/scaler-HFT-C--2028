#include "order_book.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>

namespace capstone {
    std::unique_ptr<OrderBook> createArrayOrderBook();
}

int main() {
    using namespace capstone;

    auto book = createArrayOrderBook();

    // Two buy orders at the same price.
    book->addOrder(Order{1, Side::Buy, 9999, 10, 1});
    book->addOrder(Order{2, Side::Buy, 9999, 20, 2});

    // A better bid and two sell orders.
    book->addOrder(Order{3, Side::Buy, 9950, 5, 3});
    book->addOrder(Order{4, Side::Sell, 10100, 15, 4});
    book->addOrder(Order{5, Side::Sell, 10200, 25, 5});

    BBO bbo = book->getBBO();

    assert(bbo.has_bid);
    assert(bbo.has_ask);
    assert(std::abs(bbo.bid_price - 99.99) < 1e-9);
    assert(bbo.bid_qty == 30);
    assert(std::abs(bbo.ask_price - 101.00) < 1e-9);
    assert(bbo.ask_qty == 15);

    auto bids = book->getBids(10);
    assert(bids.size() == 2);
    assert(std::abs(bids[0].price - 99.99) < 1e-9);
    assert(bids[0].qty == 30);
    assert(std::abs(bids[1].price - 99.50) < 1e-9);
    assert(bids[1].qty == 5);

    // Cancel one of the two orders at the best bid.
    assert(book->removeOrder(1));

    bbo = book->getBBO();
    assert(bbo.has_bid);
    assert(std::abs(bbo.bid_price - 99.99) < 1e-9);
    assert(bbo.bid_qty == 20);

    // An unknown order must not be cancelled successfully.
    assert(!book->removeOrder(999));

    // Cancel the remaining order at the best bid.
    assert(book->removeOrder(2));

    bbo = book->getBBO();
    assert(bbo.has_bid);
    assert(std::abs(bbo.bid_price - 99.50) < 1e-9);
    assert(bbo.bid_qty == 5);

    // Cancel the final bid and verify the empty bid side.
    assert(book->removeOrder(3));

    bbo = book->getBBO();
    assert(!bbo.has_bid);
    assert(bbo.has_ask);

    // IDs must never be reused, even after cancellation.
    bool duplicateRejected = false;

    try {
        book->addOrder(Order{1, Side::Buy, 9900, 1, 6});
    } catch (const std::invalid_argument&) {
        duplicateRejected = true;
    }

    assert(duplicateRejected);

    std::cout << "Design C tests: PASS\n";
    return 0;
}
