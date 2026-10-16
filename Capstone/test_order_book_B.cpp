
#include <cassert>
#include <iostream>

#define main benchmark_main_B
#include "Implementation_B.cpp"
#undef main

int main() {
    OrderBookB book;

    assert(book.add_order(1, true, 9990, 10));
    assert(book.add_order(2, true, 9990, 5));
    assert(book.add_order(3, false, 10010, 8));

    // Duplicate ID must be rejected.
    assert(!book.add_order(1, true, 9990, 10));

    // Check aggregated quantities and best prices.
    auto bbo = book.get_bbo();
    assert(bbo.hasBid && bbo.hasAsk);
    assert(bbo.bestBid.priceTicks == 9990);
    assert(bbo.bestBid.quantity == 15);
    assert(bbo.bestAsk.priceTicks == 10010);
    assert(bbo.bestAsk.quantity == 8);

    // Invalid prices and quantities must be rejected.
    assert(!book.add_order(4, true, 4999, 1));
    assert(!book.add_order(5, false, 15001, 1));
    assert(!book.add_order(6, true, 9990, 0));

    // Cancellation updates the aggregated quantity.
    assert(book.cancel_order(1));
    bbo = book.get_bbo();
    assert(bbo.bestBid.quantity == 5);

    // Cannot cancel twice or reuse an ID.
    assert(!book.cancel_order(1));
    assert(!book.add_order(1, true, 9990, 1));

    // Removing the final order at a price removes that level.
    assert(book.cancel_order(2));
    bbo = book.get_bbo();
    assert(!bbo.hasBid);

    // Check depth handling.
    assert(book.get_bids(10).empty());
    assert(book.get_asks(0).empty());

    std::cout << "Implementation B: all tests passed\n";
    return 0;
}
