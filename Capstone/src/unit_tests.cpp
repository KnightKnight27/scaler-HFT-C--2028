#include "Types.hpp"
#include "OrderBookA.hpp"
#include "OrderBookB.hpp"
#include "OrderBookC.hpp"

#include <iostream>
#include <cassert>
#include <vector>
#include <memory>
#include <cmath>

void run_tests_for_book(const std::string& name, std::unique_ptr<OrderBook> book) {
    std::cout << "--- Testing " << name << " ---\n";

    // 1. Empty book test
    {
        BBO bbo = book->get_bbo();
        assert(!bbo.has_bid && "Empty book must have has_bid == false");
        assert(!bbo.has_ask && "Empty book must have has_ask == false");
        assert(book->get_bids(10).empty() && "Empty book get_bids must be empty");
        assert(book->get_asks(10).empty() && "Empty book get_asks must be empty");
        assert(book->get_bids(0).empty() && "get_bids(0) must be empty");
        assert(book->get_asks(0).empty() && "get_asks(0) must be empty");
        assert(!book->cancel_order(12345) && "Cancelling non-existent order on empty book must return false");
        std::cout << "  [PASS] Empty book behavior\n";
    }

    // 2. Single order addition and BBO
    {
        book->add_order(1, Side::Buy, 75.50, 100);
        BBO bbo = book->get_bbo();
        assert(bbo.has_bid && "Should have bid");
        assert(!bbo.has_ask && "Should not have ask");
        assert(std::abs(bbo.bid_price - 75.50) < 1e-5 && "Bid price mismatch");
        assert(bbo.bid_qty == 100 && "Bid quantity mismatch");

        book->add_order(2, Side::Sell, 120.25, 50);
        bbo = book->get_bbo();
        assert(bbo.has_bid && bbo.has_ask && "Should have both bid and ask");
        assert(std::abs(bbo.ask_price - 120.25) < 1e-5 && "Ask price mismatch");
        assert(bbo.ask_qty == 50 && "Ask quantity mismatch");
        std::cout << "  [PASS] Single order addition and BBO\n";
    }

    // 3. Quantity aggregation at the same price level
    {
        book->add_order(3, Side::Buy, 75.50, 40);
        book->add_order(4, Side::Buy, 75.50, 60);
        BBO bbo = book->get_bbo();
        assert(bbo.bid_qty == 200 && "Quantity should aggregate (100 + 40 + 60 = 200)");

        // Partial cancellation of orders at that price level
        assert(book->cancel_order(3) && "Cancel order 3 should return true");
        bbo = book->get_bbo();
        assert(bbo.bid_qty == 160 && "Quantity should be 160 after cancelling order 3");

        assert(book->cancel_order(1) && "Cancel order 1 should return true");
        bbo = book->get_bbo();
        assert(bbo.bid_qty == 60 && "Quantity should be 60 after cancelling order 1");

        assert(book->cancel_order(4) && "Cancel order 4 should return true");
        bbo = book->get_bbo();
        assert(!bbo.has_bid && "Bid side should now be empty");

        // Clean up ask order 2
        assert(book->cancel_order(2) && "Cancel order 2 should return true");
        bbo = book->get_bbo();
        assert(!bbo.has_ask && "Ask side should now be empty");
        std::cout << "  [PASS] Quantity aggregation and level emptying\n";
    }

    // 4. Duplicate cancel and unknown cancel
    {
        assert(!book->cancel_order(1) && "Double-cancelling order 1 must return false");
        assert(!book->cancel_order(999999) && "Cancelling non-existent ID must return false");
        std::cout << "  [PASS] Duplicate and unknown cancel resilience\n";
    }

    // 5. Price boundaries: [50.00, 99.99] and [100.01, 150.00]
    {
        book->add_order(10, Side::Buy, 50.00, 10);   // Lowest possible bid
        book->add_order(11, Side::Buy, 99.99, 20);   // Highest possible bid
        book->add_order(12, Side::Sell, 100.01, 30); // Lowest possible ask
        book->add_order(13, Side::Sell, 150.00, 40); // Highest possible ask

        BBO bbo = book->get_bbo();
        assert(std::abs(bbo.bid_price - 99.99) < 1e-5 && "Best bid should be 99.99");
        assert(bbo.bid_qty == 20);
        assert(std::abs(bbo.ask_price - 100.01) < 1e-5 && "Best ask should be 100.01");
        assert(bbo.ask_qty == 30);

        // Cancel best bid, verify next best bid is 50.00
        assert(book->cancel_order(11));
        bbo = book->get_bbo();
        assert(std::abs(bbo.bid_price - 50.00) < 1e-5 && "Best bid should fall back to 50.00");
        assert(bbo.bid_qty == 10);

        // Cancel best ask, verify next best ask is 150.00
        assert(book->cancel_order(12));
        bbo = book->get_bbo();
        assert(std::abs(bbo.ask_price - 150.00) < 1e-5 && "Best ask should advance to 150.00");
        assert(bbo.ask_qty == 40);

        // Clean up
        assert(book->cancel_order(10));
        assert(book->cancel_order(13));
        std::cout << "  [PASS] Boundary prices [50.00, 150.00] and BBO fallback\n";
    }

    // 6. Depth ordering and Insufficient Depth queries
    {
        // Add bids: 60.00, 70.00, 80.00, 90.00, 95.00
        book->add_order(21, Side::Buy, 60.00, 5);
        book->add_order(22, Side::Buy, 90.00, 15);
        book->add_order(23, Side::Buy, 70.00, 10);
        book->add_order(24, Side::Buy, 95.00, 20);
        book->add_order(25, Side::Buy, 80.00, 25);

        auto bids_top3 = book->get_bids(3);
        assert(bids_top3.size() == 3 && "Should return top 3 levels");
        assert(std::abs(bids_top3[0].price - 95.00) < 1e-5 && bids_top3[0].qty == 20);
        assert(std::abs(bids_top3[1].price - 90.00) < 1e-5 && bids_top3[1].qty == 15);
        assert(std::abs(bids_top3[2].price - 80.00) < 1e-5 && bids_top3[2].qty == 25);

        // Request depth = 10 when only 5 levels exist
        auto bids_all = book->get_bids(10);
        assert(bids_all.size() == 5 && "Should return available 5 levels when depth=10");
        assert(std::abs(bids_all[3].price - 70.00) < 1e-5 && bids_all[3].qty == 10);
        assert(std::abs(bids_all[4].price - 60.00) < 1e-5 && bids_all[4].qty == 5);

        // Add asks: 105.00, 115.00, 110.00
        book->add_order(31, Side::Sell, 115.00, 50);
        book->add_order(32, Side::Sell, 105.00, 30);
        book->add_order(33, Side::Sell, 110.00, 40);

        auto asks_top2 = book->get_asks(2);
        assert(asks_top2.size() == 2 && "Should return top 2 asks");
        assert(std::abs(asks_top2[0].price - 105.00) < 1e-5 && asks_top2[0].qty == 30);
        assert(std::abs(asks_top2[1].price - 110.00) < 1e-5 && asks_top2[1].qty == 40);

        auto asks_all = book->get_asks(10);
        assert(asks_all.size() == 3 && "Should return available 3 levels");
        assert(std::abs(asks_all[2].price - 115.00) < 1e-5 && asks_all[2].qty == 50);

        // Cleanup
        for (uint64_t id = 21; id <= 25; ++id) assert(book->cancel_order(id));
        for (uint64_t id = 31; id <= 33; ++id) assert(book->cancel_order(id));
        std::cout << "  [PASS] Depth ordering & insufficient depth handling\n";
    }

    // 7. Large uint64_t order IDs
    {
        uint64_t large_id_1 = 0xFFFFFFFFFFFFFFFEULL;
        uint64_t large_id_2 = 1234567890123456789ULL;
        book->add_order(large_id_1, Side::Buy, 88.88, 10);
        book->add_order(large_id_2, Side::Sell, 111.11, 20);

        BBO bbo = book->get_bbo();
        assert(bbo.has_bid && bbo.has_ask);
        assert(book->cancel_order(large_id_1));
        assert(book->cancel_order(large_id_2));
        assert(!book->cancel_order(large_id_1));
        std::cout << "  [PASS] Large 64-bit uint64_t ID support\n";
    }

    std::cout << "All unit tests passed for " << name << "!\n\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "=== Running OrderBook Unit Test Suite ===\n";
    std::cout << "========================================\n\n";

    run_tests_for_book("Implementation A (std::map)", std::make_unique<OrderBookA>());
    run_tests_for_book("Implementation B (sorted std::vector)", std::make_unique<OrderBookB>());
    run_tests_for_book("Implementation C (Direct + Bitmap + Flat Hash)", std::make_unique<OrderBookC>());

    std::cout << "========================================\n";
    std::cout << "ALL THREE IMPLEMENTATIONS PASSED ALL UNIT TESTS!\n";
    std::cout << "========================================\n";
    return 0;
}
