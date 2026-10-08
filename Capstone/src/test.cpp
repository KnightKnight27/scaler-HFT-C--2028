#include "OrderBookA.hpp"
#include "OrderBookB.hpp"
#include "OrderBookC.hpp"
#include <iostream>
#include <cassert>
#include <memory>
#include <vector>

void test_orderbook_invariants(std::unique_ptr<OrderBookBase> book, const std::string& name) {
    std::cout << "Testing " << name << " ... " << std::flush;

    // 1. Empty book test
    BBO empty_bbo = book->get_bbo();
    assert(!empty_bbo.has_bid && "Empty book must not have bid");
    assert(!empty_bbo.has_ask && "Empty book must not have ask");
    assert(book->get_bids(10).empty() && "Empty book bids must be empty");
    assert(book->get_asks(10).empty() && "Empty book asks must be empty");

    // 2. Cancel non-existent order
    assert(!book->cancel_order(999999) && "Cancelling non-existent order must return false");

    // 3. Add orders to book
    // Bids:
    book->add_order(1, Side::Buy, 99.50, 10);
    book->add_order(2, Side::Buy, 99.80, 25);
    book->add_order(3, Side::Buy, 99.50, 15); // Same price level as order 1 (total qty = 25)
    book->add_order(4, Side::Buy, 98.00, 50);

    // Asks:
    book->add_order(5, Side::Sell, 100.20, 30);
    book->add_order(6, Side::Sell, 100.50, 40);
    book->add_order(7, Side::Sell, 100.20, 10); // Same price level as order 5 (total qty = 40)

    // 4. Verify BBO
    BBO bbo = book->get_bbo();
    assert(bbo.has_bid && "Must have bid");
    assert(std::abs(bbo.bid_price - 99.80) < 1e-6 && "Best bid price must be 99.80");
    assert(bbo.bid_qty == 25 && "Best bid qty must be 25");

    assert(bbo.has_ask && "Must have ask");
    assert(std::abs(bbo.ask_price - 100.20) < 1e-6 && "Best ask price must be 100.20");
    assert(bbo.ask_qty == 40 && "Best ask qty must be 40");

    // 5. Verify Depth Ordering & Aggregation
    // Bids should be strictly descending: 99.80 (25), 99.50 (25), 98.00 (50)
    auto bids = book->get_bids(10);
    assert(bids.size() == 3 && "Should have exactly 3 bid levels (insufficient depth handled)");
    assert(std::abs(bids[0].price - 99.80) < 1e-6 && bids[0].qty == 25);
    assert(std::abs(bids[1].price - 99.50) < 1e-6 && bids[1].qty == 25);
    assert(std::abs(bids[2].price - 98.00) < 1e-6 && bids[2].qty == 50);

    // Asks should be strictly ascending: 100.20 (40), 100.50 (40)
    auto asks = book->get_asks(10);
    assert(asks.size() == 2 && "Should have exactly 2 ask levels");
    assert(std::abs(asks[0].price - 100.20) < 1e-6 && asks[0].qty == 40);
    assert(std::abs(asks[1].price - 100.50) < 1e-6 && asks[1].qty == 40);

    // 6. Test Partial level cancellation
    assert(book->cancel_order(1) && "Order 1 cancel should succeed");
    bids = book->get_bids(10);
    assert(bids.size() == 3 && "Level 99.50 should still exist with order 3 remaining");
    assert(std::abs(bids[1].price - 99.50) < 1e-6 && bids[1].qty == 15);

    // Cancel order 1 again -> must return false
    assert(!book->cancel_order(1) && "Cancelling already cancelled order 1 must return false");

    // 7. Test Best Bid removal and fallback to next best
    assert(book->cancel_order(2) && "Order 2 cancel should succeed");
    bbo = book->get_bbo();
    assert(bbo.has_bid && "Must still have bid");
    assert(std::abs(bbo.bid_price - 99.50) < 1e-6 && "Best bid should fall back to 99.50");
    assert(bbo.bid_qty == 15 && "Best bid qty should be 15");

    // 8. Cancel all remaining orders
    assert(book->cancel_order(3));
    assert(book->cancel_order(4));
    assert(book->cancel_order(5));
    assert(book->cancel_order(6));
    assert(book->cancel_order(7));

    // Book should be completely empty again
    BBO cleared_bbo = book->get_bbo();
    assert(!cleared_bbo.has_bid && "Book must be empty of bids");
    assert(!cleared_bbo.has_ask && "Book must be empty of asks");
    assert(book->get_bids().empty());
    assert(book->get_asks().empty());

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=========================================================\n";
    std::cout << " Order Book Unit Tests & Specification Invariants        \n";
    std::cout << "=========================================================\n";

    test_orderbook_invariants(std::make_unique<OrderBookA>(), "OrderBookA (std::map)");
    test_orderbook_invariants(std::make_unique<OrderBookB>(), "OrderBookB (std::vector)");
    test_orderbook_invariants(std::make_unique<OrderBookC>(), "OrderBookC (Direct Tick Array)");

    std::cout << "\nAll unit test suites PASSED successfully!\n";
    return 0;
}
