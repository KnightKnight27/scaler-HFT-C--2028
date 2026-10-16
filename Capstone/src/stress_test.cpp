#include "Types.hpp"
#include "OrderBookA.hpp"
#include "OrderBookB.hpp"
#include "OrderBookC.hpp"

#include <iostream>
#include <cassert>
#include <vector>
#include <memory>
#include <cmath>

void test_all_10000_price_levels() {
    std::cout << "Testing full 10,000 price level density across all books...\n";
    auto a = std::make_unique<OrderBookA>();
    auto b = std::make_unique<OrderBookB>();
    auto c = std::make_unique<OrderBookC>();

    uint64_t id = 1;

    // Fill all 5000 bid ticks (50.00 to 99.99)
    for (int t = 0; t < 5000; ++t) {
        double p = tick_to_price(t);
        a->add_order(id, Side::Buy, p, 10);
        b->add_order(id, Side::Buy, p, 10);
        c->add_order(id, Side::Buy, p, 10);
        ++id;
    }

    // Fill all 5000 ask ticks (100.01 to 150.00)
    for (int t = 5001; t <= 10000; ++t) {
        double p = tick_to_price(t);
        a->add_order(id, Side::Sell, p, 20);
        b->add_order(id, Side::Sell, p, 20);
        c->add_order(id, Side::Sell, p, 20);
        ++id;
    }

    // Check BBO
    BBO bboA = a->get_bbo();
    BBO bboB = b->get_bbo();
    BBO bboC = c->get_bbo();
    assert(bboA == bboB && bboB == bboC);
    assert(std::abs(bboA.bid_price - 99.99) < 1e-5 && bboA.bid_qty == 10);
    assert(std::abs(bboA.ask_price - 100.01) < 1e-5 && bboA.ask_qty == 20);

    // Cancel top 100 bids and asks, verify BBO moves in lockstep across all 3 books
    uint64_t bid_cancel_id = 5000; // was added at tick 4999 (99.99)
    for (int step = 0; step < 100; ++step) {
        uint64_t cid = bid_cancel_id - step;
        assert(a->cancel_order(cid));
        assert(b->cancel_order(cid));
        assert(c->cancel_order(cid));

        BBO currA = a->get_bbo();
        BBO currB = b->get_bbo();
        BBO currC = c->get_bbo();
        assert(currA == currB && currB == currC);
        double expected_bid = tick_to_price(4999 - step - 1);
        assert(std::abs(currA.bid_price - expected_bid) < 1e-5);
    }

    std::cout << "  [PASS] 10,000 dense price level test passed!\n";
}

void test_rehash_capacity_expansion_in_c() {
    std::cout << "Testing OrderBookC dynamic capacity rehash...\n";
    // Initialize with small capacity (e.g., 1024 slots) so rehash triggers multiple times
    auto book = std::make_unique<OrderBookC>(1024);

    constexpr size_t NUM_TEST_ORDERS = 50000;
    for (size_t i = 1; i <= NUM_TEST_ORDERS; ++i) {
        double price = 50.00 + ((i % 4900) * 0.01);
        book->add_order(i, Side::Buy, price, 10);
    }

    // Cancel all test orders, verifying each is found despite multiple rehashes
    for (size_t i = 1; i <= NUM_TEST_ORDERS; ++i) {
        assert(book->cancel_order(i) && "Order must be found after rehash");
    }

    // Verify empty
    BBO bbo = book->get_bbo();
    assert(!bbo.has_bid);
    std::cout << "  [PASS] Rehash and capacity expansion test passed!\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "=== Running Stress & Density Tests =====\n";
    std::cout << "========================================\n\n";

    test_all_10000_price_levels();
    test_rehash_capacity_expansion_in_c();

    std::cout << "\n========================================\n";
    std::cout << "ALL STRESS AND DENSITY TESTS PASSED!\n";
    std::cout << "========================================\n";
    return 0;
}
